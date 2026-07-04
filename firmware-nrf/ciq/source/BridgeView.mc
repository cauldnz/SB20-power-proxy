// BridgeView — the screen: connection state, out/in watts, correction, recording state +
// sample count. SELECT toggles recording (see BridgeInput).
using Toybox.WatchUi;
using Toybox.Graphics;

class BridgeView extends WatchUi.View {
    hidden var _ble;

    function initialize(ble) {
        View.initialize();
        _ble = ble;
    }

    function refresh() {
        WatchUi.requestUpdate();
    }

    function onUpdate(dc) {
        dc.setColor(Graphics.COLOR_BLACK, Graphics.COLOR_BLACK);
        dc.clear();
        var w = dc.getWidth();
        var y = dc.getHeight() / 8;

        dc.setColor(Graphics.COLOR_LT_GRAY, Graphics.COLOR_TRANSPARENT);
        dc.drawText(w / 2, y, Graphics.FONT_SMALL,
                    _ble.connected() ? "BRIDGE LINKED" : "searching...",
                    Graphics.TEXT_JUSTIFY_CENTER);

        var st = _ble.status;
        if (st != null) {
            dc.setColor(Graphics.COLOR_WHITE, Graphics.COLOR_TRANSPARENT);
            var outW = st[:outW] >= 0 ? st[:outW].toString() : "--";
            dc.drawText(w / 2, y * 2, Graphics.FONT_NUMBER_HOT, outW,
                        Graphics.TEXT_JUSTIFY_CENTER);
            dc.setColor(Graphics.COLOR_LT_GRAY, Graphics.COLOR_TRANSPARENT);
            var srcW = st[:srcW] >= 0 ? st[:srcW].toString() : "--";
            var corr = "in " + srcW + "W  x" + (st[:scaleMilli] / 1000.0).format("%.3f");
            dc.drawText(w / 2, y * 4, Graphics.FONT_SMALL, corr, Graphics.TEXT_JUSTIFY_CENTER);
        }

        var rec = _ble.recState == 1;
        dc.setColor(rec ? Graphics.COLOR_RED : Graphics.COLOR_LT_GRAY,
                    Graphics.COLOR_TRANSPARENT);
        var recTxt = rec ? "REC " + _ble.recSamples : "press SELECT to record";
        dc.drawText(w / 2, y * 5.5, Graphics.FONT_SMALL, recTxt, Graphics.TEXT_JUSTIFY_CENTER);
    }
}

class BridgeInput extends WatchUi.BehaviorDelegate {
    hidden var _ble;

    function initialize(ble) {
        BehaviorDelegate.initialize();
        _ble = ble;
    }

    // SELECT (Edge: the start/lap area; Epix: the upper-right button) toggles recording.
    function onSelect() {
        _ble.sendRecCmd(_ble.recState == 1 ? 0 : 1);
        return true;
    }
}
