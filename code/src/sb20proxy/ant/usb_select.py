"""Pin openant to a SPECIFIC ANT+ stick when several are attached.

openant's `Node()` always grabs the first `0fcf` device (`usb.core.find` with no
selection), so two Nodes on one host fight over the same stick. openant claims the
device synchronously inside `Node()` (Ant.__init__ -> find_driver().open()), so we
can wrap each `Node()` construction in `pinned_stick(dev)` to bind it to a chosen
stick — letting one process run a transmitter on stick 0 and a receiver on stick 1
(the on-air loopback), or capture-while-spoofing later.

Hardware-only (needs pyusb + real sticks); imported lazily from the radio paths so
the software / CI path never touches it.
"""

from __future__ import annotations

import contextlib
import time

import usb.core  # pyusb (an openant dependency)

ANT_VID = 0x0FCF
ANT_PIDS = (0x1008, 0x1009)  # ANTUSB2 Stick, ANT-USB-m


def list_ant_sticks() -> list:
    """All attached ANT+ sticks, ordered by (bus, address) for stable indices."""
    found: list = []
    for pid in ANT_PIDS:
        found += list(usb.core.find(find_all=True, idVendor=ANT_VID, idProduct=pid) or [])
    return sorted(found, key=lambda d: (d.bus, d.address))


def describe_sticks() -> str:
    sticks = list_ant_sticks()
    if not sticks:
        return "(no ANT+ sticks found — attach one to WSL with usbipd)"
    return "; ".join(
        f"[{i}] bus {d.bus} addr {d.address} pid {d.idProduct:#06x}"
        for i, d in enumerate(sticks)
    )


def select_ant_stick(index: int = 0):
    """Return the Nth ANT+ stick (by stable bus/address order)."""
    sticks = list_ant_sticks()
    if not sticks:
        raise RuntimeError("no ANT+ sticks found (is one attached to WSL?)")
    if index >= len(sticks):
        raise RuntimeError(f"requested stick index {index} but only {len(sticks)} attached")
    return sticks[index]


def reset_node(node) -> None:
    """Clear stale ANT channel state before assigning a channel.

    openant resets once in its constructor, but a channel left ASSIGNED by a
    process that was killed (kill -9, a crash) survives on the chip and makes the
    next `new_channel` fail with CHANNEL_IN_WRONG_STATE. An explicit reset_system
    here makes open() self-heal from that.
    """
    try:
        node.ant.reset_system()
        time.sleep(0.6)
    except Exception:
        pass


@contextlib.contextmanager
def pinned_stick(dev):
    """Force openant's `usb.core.find` to return `dev` while a Node is constructed.

    `dev` may be None (no pinning — use the first/only stick). The patch respects the
    requested VID/PID so openant still selects the right driver class.
    """
    if dev is None:
        yield
        return
    original = usb.core.find

    def patched(*args, find_all=False, **kwargs):
        if find_all:
            return original(*args, find_all=True, **kwargs)
        vid = kwargs.get("idVendor")
        pid = kwargs.get("idProduct")
        if vid in (None, dev.idVendor) and pid in (None, dev.idProduct):
            return dev
        return original(*args, **kwargs)

    usb.core.find = patched
    try:
        yield
    finally:
        usb.core.find = original
