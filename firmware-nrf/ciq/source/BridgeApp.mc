// BridgeApp — app entry: owns the BLE layer, wires the view + input delegate.
using Toybox.Application;
using Toybox.BluetoothLowEnergy as Ble;

class BridgeApp extends Application.AppBase {
    var ble;

    function initialize() {
        AppBase.initialize();
    }

    function onStart(state) {
        ble = new BridgeBle();
        Ble.setDelegate(ble);
        ble.start();
    }

    function onStop(state) {
        Ble.setScanState(Ble.SCAN_STATE_OFF);
    }

    function getInitialView() {
        var view = new BridgeView(ble);
        ble.onUpdate = view.method(:refresh);
        return [view, new BridgeInput(ble)];
    }
}
