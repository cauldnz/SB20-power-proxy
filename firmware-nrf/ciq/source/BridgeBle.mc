// BridgeBle — the BLE layer: scan for the bridge, connect, subscribe Status/RecCtl, write
// RecCtl commands. Mirrors ../GATT.md (PROTO_VER 1) byte for byte; keep in lockstep with
// firmware lib/bridge/Proto.h and the web app.
using Toybox.BluetoothLowEnergy as Ble;
using Toybox.System;

class BridgeBle extends Ble.BleDelegate {
    // 53423230-XXXX-4bd9-a4ae-1b4e2c633a1d
    const SVC     = Ble.stringToUuid("53423230-0000-4bd9-a4ae-1b4e2c633a1d");
    const CH_STAT = Ble.stringToUuid("53423230-0001-4bd9-a4ae-1b4e2c633a1d");
    const CH_RCTL = Ble.stringToUuid("53423230-0003-4bd9-a4ae-1b4e2c633a1d");

    hidden var _profileRegistered = false;
    hidden var _device = null;
    hidden var _rctl = null;
    var status = null;        // dict updated from Status notifies
    var recState = 0;         // 0 idle / 1 recording / 2 downloading
    var recSamples = 0;
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
            var stat = svc.getCharacteristic(CH_STAT);
            if (stat != null) {
                var cccd = stat.getDescriptor(Ble.cccdUuid());
                if (cccd != null) { cccd.requestWrite([0x01, 0x00]b); }
            }
        } else {
            _device = null;
            _rctl = null;
            if (_profileRegistered) { Ble.setScanState(Ble.SCAN_STATE_SCANNING); }
        }
        if (onUpdate != null) { onUpdate.invoke(); }
    }

    function onDescriptorWrite(descriptor, ble_status) {
        // Status CCCD written -> also enable RecCtl notifications (one write in flight at a time)
        if (_rctl != null) {
            var cccd = _rctl.getDescriptor(Ble.cccdUuid());
            if (cccd != null) { cccd.requestWrite([0x01, 0x00]b); }
        }
    }

    function onCharacteristicChanged(ch, value) {
        if (ch.getUuid().equals(CH_STAT) && value.size() >= 20) {
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
        } else if (ch.getUuid().equals(CH_RCTL) && value.size() >= 12) {
            recState = value[1];
            recSamples = value[4] | (value[5] << 8) | (value[6] << 16) | (value[7] << 24);
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
}
