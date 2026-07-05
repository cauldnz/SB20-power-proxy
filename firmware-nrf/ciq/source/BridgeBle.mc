// BridgeBle — the BLE layer: scan for the bridge, connect, subscribe Status/RecCtl/Workout, write
// RecCtl + Workout commands. Mirrors ../GATT.md (PROTO_VER 1) byte for byte; keep in lockstep with
// firmware lib/bridge/Proto.h and the web app.
using Toybox.BluetoothLowEnergy as Ble;
using Toybox.System;

class BridgeBle extends Ble.BleDelegate {
    // 53423230-XXXX-4bd9-a4ae-1b4e2c633a1d
    const SVC     = Ble.stringToUuid("53423230-0000-4bd9-a4ae-1b4e2c633a1d");
    const CH_STAT = Ble.stringToUuid("53423230-0001-4bd9-a4ae-1b4e2c633a1d");
    const CH_RCTL = Ble.stringToUuid("53423230-0003-4bd9-a4ae-1b4e2c633a1d");
    const CH_WK   = Ble.stringToUuid("53423230-0008-4bd9-a4ae-1b4e2c633a1d");

    hidden var _profileRegistered = false;
    hidden var _device = null;
    hidden var _rctl = null;
    hidden var _wk = null;
    hidden var _toEnable = [];   // characteristics whose CCCD still needs a notify-enable write
    var status = null;        // dict updated from Status notifies
    var recState = 0;         // 0 idle / 1 recording / 2 downloading
    var recSamples = 0;
    var wk = null;            // dict updated from Workout notifies (erg state)
    var onUpdate = null;      // callback for the view

    function initialize() {
        BleDelegate.initialize();
    }

    function start() {
        var profile = {
            :uuid => SVC,
            :characteristics => [
                { :uuid => CH_STAT, :descriptors => [Ble.cccdUuid()] },
                { :uuid => CH_RCTL, :descriptors => [Ble.cccdUuid()] },
                { :uuid => CH_WK,   :descriptors => [Ble.cccdUuid()] },
            ],
        };
        try {
            Ble.registerProfile(profile);
        } catch (e) {
            System.println("profile register failed: " + e.getErrorMessage());
        }
    }

    function onProfileRegister(uuid, s) {
        _profileRegistered = true;
        Ble.setScanState(Ble.SCAN_STATE_SCANNING);
    }

    function onScanResults(scanResults) {
        // Iterator.next() is typed as Object? — cast each result to ScanResult before use.
        for (var r = scanResults.next(); r != null; r = scanResults.next()) {
            var sr = r as Ble.ScanResult;
            var name = sr.getDeviceName();
            if (name != null && name.find("SB20") != null) {
                Ble.setScanState(Ble.SCAN_STATE_OFF);
                Ble.pairDevice(sr);
                return;
            }
        }
    }

    function onConnectedStateChanged(device, state) {
        if (state == Ble.CONNECTION_STATE_CONNECTED) {
            _device = device;
            var svc = device.getService(SVC);
            if (svc == null) { return; }
            _rctl = svc.getCharacteristic(CH_RCTL);
            _wk = svc.getCharacteristic(CH_WK);
            // Enable notifications on every characteristic that has them, one CCCD write at a time
            // (the stack allows a single write in flight — onDescriptorWrite advances the queue).
            _toEnable = [];
            var stat = svc.getCharacteristic(CH_STAT);
            if (stat != null) { _toEnable.add(stat); }
            if (_rctl != null) { _toEnable.add(_rctl); }
            if (_wk != null) { _toEnable.add(_wk); }
            enableNext();
        } else {
            _device = null;
            _rctl = null;
            _wk = null;
            _toEnable = [];
            if (_profileRegistered) { Ble.setScanState(Ble.SCAN_STATE_SCANNING); }
        }
        if (onUpdate != null) { onUpdate.invoke(); }
    }

    // Pop the next characteristic and enable its CCCD; called on connect and after each write.
    hidden function enableNext() {
        if (_toEnable.size() == 0) { return; }
        var ch = _toEnable[0];
        _toEnable = _toEnable.slice(1, null);
        var cccd = ch.getDescriptor(Ble.cccdUuid());
        if (cccd != null) { cccd.requestWrite([0x01, 0x00]b); }
        else { enableNext(); }
    }

    function onDescriptorWrite(descriptor, ble_status) {
        enableNext();
    }

    function onCharacteristicChanged(ch, value) {
        var uuid = ch.getUuid();
        if (uuid.equals(CH_STAT) && value.size() >= 20) {
            status = {
                :srcConnected => (value[1] & 0x01) != 0,
                :recording => (value[1] & 0x04) != 0,
                :srcW => u16(value, 2),
                :outW => u16(value, 4),
                :cad => u16(value, 6),
                :scaleMilli => value[10] | (value[11] << 8),
                :offsetDeci => s16(value, 12),
                :recSamples => value[14] | (value[15] << 8) | (value[16] << 16) | (value[17] << 24),
            };
            recSamples = status[:recSamples];
        } else if (uuid.equals(CH_RCTL) && value.size() >= 12) {
            recState = value[1];
            recSamples = value[4] | (value[5] << 8) | (value[6] << 16) | (value[7] << 24);
        } else if (uuid.equals(CH_WK) && value.size() >= 18) {
            var flags = value[1];
            wk = {
                :loaded => (flags & 0x01) != 0,
                :running => (flags & 0x02) != 0,
                :paused => (flags & 0x04) != 0,
                :ergConnected => (flags & 0x08) != 0,
                :ergControlled => (flags & 0x10) != 0,
                :target => s16(value, 2),
                :segIndex => value[4],
                :nSeg => value[5],
                :bias => s16(value, 12),
            };
        }
        if (onUpdate != null) { onUpdate.invoke(); }
    }

    hidden function u16(b, off) {
        var v = b[off] | (b[off + 1] << 8);
        return v > 32767 ? v - 65536 : v;
    }
    hidden function s16(b, off) { return u16(b, off); }

    function connected() { return _device != null; }

    // RecCtl commands: 0 stop / 1 start / 2 erase
    function sendRecCmd(cmd) {
        if (_rctl == null) { return; }
        _rctl.requestWrite([0x01, cmd]b, { :writeType => Ble.WRITE_TYPE_WITH_RESPONSE });
    }

    // Workout commands (WkCmd): 3 start / 4 pause / 5 resume / 6 stop.
    function sendWkCmd(cmd) {
        if (_wk == null) { return; }
        _wk.requestWrite([0x01, cmd]b, { :writeType => Ble.WRITE_TYPE_WITH_RESPONSE });
    }

    // Shifter: nudge the erg target by delta W (signed). Cmd 8 = BiasStep, delta as an i8 byte.
    function sendBias(delta) {
        if (_wk == null) { return; }
        var b = delta < 0 ? (256 + delta) : delta;   // two's-complement i8
        _wk.requestWrite([0x01, 0x08, b]b, { :writeType => Ble.WRITE_TYPE_WITH_RESPONSE });
    }

    // The workout is startable/pausable only once an erg session is loaded + a trainer is linked.
    function ergReady() { return wk != null && wk[:ergConnected]; }
}
