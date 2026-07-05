// BridgeView — the screen: connection state, out/in watts, correction, the erg/workout line, and
// recording state. Inputs (BridgeInput): SELECT toggles recording · MENU starts/pauses the loaded
// workout · UP/DOWN are the "shifter" (nudge the erg target ±10 W). Workout SETUP (pick trainer,
// load preset) lives in the web app; the Garmin is the in-ride controller.
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
        var y = dc.getHeight() / 8.0;

        dc.setColor(Graphics.COLOR_LT_GRAY, Graphics.COLOR_TRANSPARENT);
        dc.drawText(w / 2, y * 0.7, Graphics.FONT_SMALL,
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
            dc.drawText(w / 2, y * 3.9, Graphics.FONT_SMALL, corr, Graphics.TEXT_JUSTIFY_CENTER);
        }

        // Erg / workout line — only when a workout is loaded on the bridge.
        var wk = _ble.wk;
        if (wk != null && wk[:loaded]) {
            var txt;
            var col = Graphics.COLOR_LT_GRAY;
            if (wk[:running]) {
                var bias = wk[:bias];
                var biasTxt = (bias >= 0 ? "+" : "") + bias;
                txt = "ERG " + wk[:target] + "W  " + biasTxt;
                col = wk[:ergControlled] ? Graphics.COLOR_GREEN : Graphics.COLOR_YELLOW;
            } else if (wk[:paused]) {
                txt = "ERG paused - MENU=resume";
            } else {
                txt = "ERG ready - MENU=start";
            }
            dc.setColor(col, Graphics.COLOR_TRANSPARENT);
            dc.drawText(w / 2, y * 5.1, Graphics.FONT_SMALL, txt, Graphics.TEXT_JUSTIFY_CENTER);
        }

        var rec = _ble.recState == 1;
        dc.setColor(rec ? Graphics.COLOR_RED : Graphics.COLOR_LT_GRAY,
                    Graphics.COLOR_TRANSPARENT);
        var recTxt = rec ? "REC " + _ble.recSamples : "SELECT to record";
        dc.drawText(w / 2, y * 6.5, Graphics.FONT_SMALL, recTxt, Graphics.TEXT_JUSTIFY_CENTER);
    }
}

class BridgeInput extends WatchUi.BehaviorDelegate {
    hidden var _ble;

    function initialize(ble) {
        BehaviorDelegate.initialize();
        _ble = ble;
    }

    // SELECT toggles recording.
    function onSelect() {
        _ble.sendRecCmd(_ble.recState == 1 ? 0 : 1);
        return true;
    }

    // MENU starts / pauses / resumes the loaded workout (WkCmd 3 start · 4 pause · 5 resume).
    function onMenu() {
        var wk = _ble.wk;
        if (wk == null || !wk[:loaded]) { return true; }
        if (!wk[:running]) { _ble.sendWkCmd(3); }        // start
        else if (wk[:paused]) { _ble.sendWkCmd(5); }     // resume
        else { _ble.sendWkCmd(4); }                      // pause
        return true;
    }

    // The "shifter": UP/DOWN nudge the erg target ±10 W while a workout runs.
    function onPreviousPage() {   // UP
        _ble.sendBias(10);
        return true;
    }
    function onNextPage() {       // DOWN
        _ble.sendBias(-10);
        return true;
    }
}
