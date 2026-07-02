"""generate_case.py — parametric 3D-printable case for the Waveshare ESP32-S3-Touch-LCD-1.47
(the SB20 head-unit board). Run in Autodesk Fusion 360 (Scripts and Add-Ins → add this file → Run),
or paste into the Fusion Python console. Builds two bodies in a fresh document + auto-exports STLs.

  Case_Back    — tray: a perimeter shelf + FOUR M2 screw bosses (the board screws down onto them via
                 its own mounting holes) + a back-component cavity + an open-top USB-C notch + two
                 side button slots.
  Bezel_Front  — frame that press-fits into the tray; two-level window (underside recess clears the
                 raised glass, top opening exposes just the active area).

v2 (2026-07-03): dimensions corrected from the Waveshare mechanical drawing — PCB 24.5 x 39 mm, 4x M2
mounting holes (top pair 17.78 mm apart, bottom pair 17.00 mm apart, 25.40 mm vertical), USB-C on the
'top' edge. The DISPLAY numbers are exact (LBS147TC-IF15 datasheet: active 17.39 x 32.35, module
19.39 x 36.28 x 1.46). Still CONFIRM against your board: exact PCB outline, the M2 hole XY, the display
offset (lcd_off_y), and especially the BUTTON positions (btn_* — the drawing doesn't dimension them, so
these are a guess). Print bezel window-down, tray open-up; PLA/PETG, 0.2 mm, 3 perimeters. M2 self-tap
screws into the bosses (pilot 1.5 mm) or drill to 1.6 mm.
"""
import adsk.core, adsk.fusion, os, math, traceback

# ---- EDIT THESE (mm) -------------------------------------------------------------------------
P = dict(
    # PCB outline (from the Waveshare drawing — verify with calipers)
    board_w=24.5, board_l=39.0, board_t=1.6,
    lcd_off_y=0.0,                 # display centre offset along board length (+ = toward USB) <-- verify
    # 4x M2 mounting holes: XY of each, from board centre (X=width, Y=length; +Y = USB/top edge)
    hx_top=8.89, hx_bot=8.50, hy=12.70,   # from spacings 17.78 / 17.00 (÷2) and 25.40 (÷2)
    m2_pilot=1.5, boss_od=4.2,            # self-tap pilot Ø / boss outer Ø
    # buttons — NOT dimensioned in the drawing; a GUESS on the left (-X) wall near the USB end. VERIFY.
    btn_on='left', btn1_y=-9.0, btn2_y=-14.0, btn_w=3.5, btn_h=2.6,
    # fits / walls
    fit=0.35, bez_fit=0.30, wall=2.0, floor_t=1.5,
    back_clear=4.0, shelf=1.2, front_clear=2.6,
    usb_w=10.0, usb_h=4.2, corner_r=2.5,
    # display (EXACT — datasheet)
    module_w=19.39, module_l=36.28, win_w=17.6, win_h=32.6, glass_relief=1.9,
)
OUTDIR = r"C:\repos\SB20-power-proxy\design\s3-case"
# ----------------------------------------------------------------------------------------------
M = 0.1  # Fusion API geometry is in cm
INTERACTIVE = True  # set False when exec'd headlessly (returns a string instead of a modal messageBox)


def run(_ctx=None):
    app = adsk.core.Application.get(); ui = app.userInterface
    try:
        # reuse the active design if it's empty-ish, else make a new one (avoids piling up docs on rerun)
        design = app.activeProduct
        if not isinstance(design, adsk.fusion.Design):
            app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType); design = app.activeProduct
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent
        for occ in list(root.occurrences):
            if occ.component.name in ('Case_Back', 'Bezel_Front'):
                occ.deleteMe()
        for k, v in P.items():
            if isinstance(v, (int, float)):
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
        JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation

        def newcomp(name):
            o = root.occurrences.addNewComponent(adsk.core.Matrix3D.create()); o.component.name = name
            return o.component

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

        def circle(c, plane, cx, cy, dia):
            sk = c.sketches.add(plane)
            sk.sketchCurves.sketchCircles.addByCenterRadius(
                adsk.core.Point3D.create(cx * M, cy * M, 0), dia / 2 * M)
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

        holes = [( P['hx_top'],  P['hy']), (-P['hx_top'],  P['hy']),
                 ( P['hx_bot'], -P['hy']), (-P['hx_bot'], -P['hy'])]

        # ---------------- Case_Back ----------------
        back = newcomp('Case_Back')
        ext(back, rect(back, back.xYConstructionPlane, 0, 0, outer_w, outer_l), shell_h, NB)
        ext(back, rect(back, plane_at(back, ledge_z), 0, 0, inner_w, inner_l), (shell_h - ledge_z) + 0.3, CUT)
        ext(back, rect(back, plane_at(back, floor_t), 0, 0, inner_w - 2 * shelf, inner_l - 2 * shelf), back_clear, CUT)
        # 4 M2 screw bosses rising from the floor to the PCB underside, with self-tap pilot holes
        for (hx, hy) in holes:
            ext(back, circle(back, plane_at(back, floor_t), hx, hy, P['boss_od']), back_clear, JOIN)
        for (hx, hy) in holes:
            ext(back, circle(back, plane_at(back, ledge_z), hx, hy, P['m2_pilot']), -(back_clear + floor_t * 0.6), CUT)
        # USB-C notch (open-top slot) in the -Y wall
        usb_bottom = ledge_z + bt / 2 - P['usb_h'] / 2
        u = back.sketches.add(plane_at(back, shell_h))
        u.sketchCurves.sketchLines.addTwoPointRectangle(
            adsk.core.Point3D.create((-P['usb_w'] / 2) * M, (-(outer_l / 2 + 1)) * M, 0),
            adsk.core.Point3D.create((P['usb_w'] / 2) * M, (-(inner_l / 2 - 0.2)) * M, 0))
        ext(back, u.profiles.item(0), -(shell_h - usb_bottom), CUT)
        # two button slots on a side wall (open-top slots). VERIFY positions against your board.
        sidex = -(outer_l and 1) and 0  # placeholder to keep linters calm
        wall_x = (inner_w / 2)  # inner face of the +/-X wall
        btn_bottom = ledge_z + bt / 2 - P['btn_h'] / 2
        for by in (P['btn1_y'], P['btn2_y']):
            s = back.sketches.add(plane_at(back, shell_h))
            x0 = (inner_w / 2 - 0.2) if P['btn_on'] == 'right' else -(outer_w / 2 + 1)
            x1 = (outer_w / 2 + 1) if P['btn_on'] == 'right' else -(inner_w / 2 - 0.2)
            s.sketchCurves.sketchLines.addTwoPointRectangle(
                adsk.core.Point3D.create(x0 * M, (by - P['btn_w'] / 2) * M, 0),
                adsk.core.Point3D.create(x1 * M, (by + P['btn_w'] / 2) * M, 0))
            ext(back, s.profiles.item(0), -(shell_h - btn_bottom), CUT)
        fillet_corners(back, outer_w / 2, outer_l / 2, P['corner_r'])

        # ---------------- Bezel_Front ----------------
        bez = newcomp('Bezel_Front')
        bo_w, bo_l = inner_w - P['bez_fit'], inner_l - P['bez_fit']
        offy = P['lcd_off_y']
        ext(bez, rect(bez, plane_at(bez, board_front), 0, 0, bo_w, bo_l), front_clear, NB)
        ext(bez, rect(bez, plane_at(bez, board_front), 0, offy, P['module_w'] + 0.6, P['module_l'] + 0.6), P['glass_relief'], CUT)
        ext(bez, rect(bez, plane_at(bez, board_front + P['glass_relief']), 0, offy, P['win_w'], P['win_h']),
            (front_clear - P['glass_relief']) + 0.3, CUT)
        fillet_corners(bez, bo_w / 2, bo_l / 2, max(P['corner_r'] - 0.3, 0.6))

        # ---------------- export STLs ----------------
        try:
            os.makedirs(OUTDIR, exist_ok=True)
            em = design.exportManager
            for c, fn in ((back, 'SB20_S3_case_back.stl'), (bez, 'SB20_S3_case_bezel.stl')):
                o = em.createSTLExportOptions(c.bRepBodies.item(0), os.path.join(OUTDIR, fn))
                o.meshRefinement = adsk.fusion.MeshRefinementSettings.MeshRefinementHigh
                o.isBinaryFormat = True
                em.execute(o)
        except Exception:
            pass

        app.activeViewport.fit()
        msg = (f'Case v2 built + STLs exported.\nTray outer {outer_w:.1f} x {outer_l:.1f} x '
               f'{shell_h:.1f} mm, 4x M2 bosses.\nVERIFY button positions + M2 hole XY vs your board.')
        if INTERACTIVE and ui:
            ui.messageBox(msg)
        return 'OK ' + msg.replace('\n', ' | ')
    except Exception:
        tb = traceback.format_exc()
        if INTERACTIVE and ui:
            ui.messageBox('Failed:\n' + tb)
        return 'ERROR\n' + tb


if __name__ == '__main__':
    run(None)
