"""generate_case.py — parametric 3D-printable case for the Waveshare ESP32-S3-Touch-LCD-1.47
(the SB20 head-unit board). Run inside Autodesk Fusion 360 (Scripts and Add-Ins → Add → this file),
or paste into the Fusion Python console. Produces two bodies in a fresh document:

  Case_Back    — tray: PCB shelf + back-component cavity + USB-C notch (open-top slot, print-friendly)
  Bezel_Front  — frame that press-fits into the tray, two-level window (clears the raised glass,
                 exposes the active area), retains the board.

Units are mm. The DISPLAY numbers are EXACT (LBS147TC-IF15 datasheet: active area 17.39 x 32.35,
module 19.39 x 36.28 x 1.46). The BOARD OUTLINE (board_w/board_l/board_t) and the display's position
along the board (lcd_off_y) are ESTIMATES — measure your PCB with calipers and edit P{} below, then
re-run. Everything else derives from those. Print the bezel face-down (window on the bed) and the tray
open-side up; PLA/PETG, 0.2 mm layers, 3 perimeters. Tune `fit`/`bez_fit` for your printer.
"""
import adsk.core, adsk.fusion, os, traceback

# ---- EDIT THESE (mm) -------------------------------------------------------------------------
P = dict(
    board_w=26.0, board_l=48.0, board_t=1.6,   # PCB outline (X width, Y length, Z)  <-- MEASURE
    lcd_off_y=0.0,                              # display centre offset along board length <-- MEASURE
    # --- fits / walls (tune for your printer) ---
    fit=0.35, bez_fit=0.30, wall=2.0, floor_t=1.5,
    back_clear=4.0, shelf=1.5, front_clear=2.6,
    usb_w=10.0, usb_h=4.2,                      # USB-C opening (bottom -Y edge)
    corner_r=2.5,
    # --- display (EXACT — datasheet) ---
    module_w=19.39, module_l=36.28, win_w=17.6, win_h=32.6, glass_relief=1.9,
)
# ----------------------------------------------------------------------------------------------

M = 0.1  # Fusion API geometry is in cm; multiply mm by this


def run(_ctx=None):
    app = adsk.core.Application.get(); ui = app.userInterface
    try:
        doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = app.activeProduct
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent

        for k, v in P.items():
            nm = 'p_' + k
            if not design.userParameters.itemByName(nm):
                try: design.userParameters.add(nm, adsk.core.ValueInput.createByString(f'{v} mm'), 'mm', 'case')
                except Exception: pass

        bw, bl, bt = P['board_w'], P['board_l'], P['board_t']
        fit, wall, floor_t = P['fit'], P['wall'], P['floor_t']
        back_clear, shelf, front_clear = P['back_clear'], P['shelf'], P['front_clear']
        inner_w, inner_l = bw + 2 * fit, bl + 2 * fit
        outer_w, outer_l = inner_w + 2 * wall, inner_l + 2 * wall
        shell_h = floor_t + back_clear + bt + front_clear
        ledge_z = floor_t + back_clear
        board_front = ledge_z + bt

        NB = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
        CUT = adsk.fusion.FeatureOperations.CutFeatureOperation

        def build(name):
            occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
            c = occ.component; c.name = name
            return c

        def plane_at(c, z):
            i = c.constructionPlanes.createInput()
            i.setByOffset(c.xYConstructionPlane, adsk.core.ValueInput.createByReal(z * M))
            return c.constructionPlanes.add(i)

        def rect(c, plane, cx, cy, w, l):
            sk = c.sketches.add(plane)
            sk.sketchCurves.sketchLines.addTwoPointRectangle(
                adsk.core.Point3D.create((cx - w / 2) * M, (cy - l / 2) * M, 0),
                adsk.core.Point3D.create((cx + w / 2) * M, (cy + l / 2) * M, 0))
            return sk.profiles.item(0)

        def ext(c, prof, dist, op):
            return c.features.extrudeFeatures.addSimple(prof, adsk.core.ValueInput.createByReal(dist * M), op)

        def fillet_corners(c, halfx, halfy, r):
            col = adsk.core.ObjectCollection.create()
            for e in c.bRepBodies.item(0).edges:
                g = e.geometry
                if isinstance(g, adsk.core.Line3D):
                    a, b = g.startPoint, g.endPoint
                    if abs(a.x - b.x) < 1e-3 and abs(a.y - b.y) < 1e-3 and abs(a.z - b.z) > 1e-3:
                        if abs(abs(a.x) / M - halfx) < 0.3 and abs(abs(a.y) / M - halfy) < 0.3:
                            col.add(e)
            if col.count:
                fi = c.features.filletFeatures.createInput()
                fi.addConstantRadiusEdgeSet(col, adsk.core.ValueInput.createByReal(r * M), True)
                c.features.filletFeatures.add(fi)

        # ---- Case_Back ----
        back = build('Case_Back')
        ext(back, rect(back, back.xYConstructionPlane, 0, 0, outer_w, outer_l), shell_h, NB)
        ext(back, rect(back, plane_at(back, ledge_z), 0, 0, inner_w, inner_l), (shell_h - ledge_z) + 0.3, CUT)
        ext(back, rect(back, plane_at(back, floor_t), 0, 0, inner_w - 2 * shelf, inner_l - 2 * shelf), back_clear, CUT)
        usb_bottom = ledge_z + bt / 2 - P['usb_h'] / 2
        usb = back.sketches.add(plane_at(back, shell_h))
        usb.sketchCurves.sketchLines.addTwoPointRectangle(
            adsk.core.Point3D.create((-P['usb_w'] / 2) * M, (-(outer_l / 2 + 1)) * M, 0),
            adsk.core.Point3D.create((P['usb_w'] / 2) * M, (-(inner_l / 2 - 0.2)) * M, 0))
        ext(back, usb.profiles.item(0), -(shell_h - usb_bottom), CUT)
        fillet_corners(back, outer_w / 2, outer_l / 2, P['corner_r'])

        # ---- Bezel_Front ----
        bez = build('Bezel_Front')
        bo_w, bo_l = inner_w - P['bez_fit'], inner_l - P['bez_fit']
        offy = P['lcd_off_y']
        ext(bez, rect(bez, plane_at(bez, board_front), 0, 0, bo_w, bo_l), front_clear, NB)
        ext(bez, rect(bez, plane_at(bez, board_front), 0, offy, P['module_w'] + 0.6, P['module_l'] + 0.6), P['glass_relief'], CUT)
        ext(bez, rect(bez, plane_at(bez, board_front + P['glass_relief']), 0, offy, P['win_w'], P['win_h']),
            (front_clear - P['glass_relief']) + 0.3, CUT)
        fillet_corners(bez, bo_w / 2, bo_l / 2, max(P['corner_r'] - 0.3, 0.6))

        app.activeViewport.fit()
        if ui:
            ui.messageBox(f'Case generated.\nOuter {outer_w:.1f} x {outer_l:.1f} x {shell_h:.1f} mm.\n'
                          'Export each body to STL (right-click body → Save As Mesh).')
    except Exception:
        if ui:
            ui.messageBox('Failed:\n{}'.format(traceback.format_exc()))


if __name__ == '__main__':
    run(None)
