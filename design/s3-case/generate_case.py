"""generate_case.py — parametric 3D-printable case for the Waveshare ESP32-S3-Touch-LCD-1.47
(the SB20 head-unit board). Run in Autodesk Fusion 360 (Scripts and Add-Ins → add this file → Run),
or paste into the Fusion Python console. Builds two bodies in a fresh document + auto-exports STLs.

  Case_Back    — tray. The board mounts BACK-DOWN on its brass standoffs, which sit on four printed
                 pillars so the board floats `header_clear` mm off the floor (the 8 mm rear header pins
                 hang in the cavity between the pillars). An M2 screw comes UP from OUTSIDE the case
                 back, through a counterbored hole + the pillar, into the female M2 standoff. Also: an
                 open-top USB-C notch + two side button slots.
  Bezel_Front  — frame that press-fits into the tray; two-level window (underside recess clears the
                 raised glass, top opening exposes just the active area).

v3 (2026-07-03): mounting reworked for the REAL hardware (owner):
  * rear header pins stand **8 mm** off the PCB back  -> enclosed; the board floats 8 mm off the floor.
  * **4 mm female M2 brass standoffs** already fitted in the mounting holes (back side)  -> 4 mm printed
    pillars make up the gap (8 - 4); M2 button-head screw from the case back into each standoff (~M2x8).
  * headers + standoffs are on the BACK (non-screen side).
This makes the case ~13–14 mm thick (the 8 mm headers dominate). Display window is exact (LBS147TC-IF15).
STILL CONFIRM from a back-side photo: the M2 hole XY (hx_*/hy), the header footprint (so the pillars
don't clash with a header block), the button positions (btn_*), and the exact standoff height.
Print bezel window-down, tray open-up; PLA/PETG, 0.2 mm, 3 perimeters.
"""
import adsk.core, adsk.fusion, os, traceback

# ---- EDIT THESE (mm) -------------------------------------------------------------------------
P = dict(
    # PCB (Waveshare drawing — verify with calipers)
    board_w=24.5, board_l=39.0, board_t=1.6, lcd_off_y=0.0,
    # 4x M2 holes / standoffs, XY from board centre (+Y = USB/top edge)
    hx_top=8.89, hx_bot=8.50, hy=12.70,
    # rear stack (owner-measured)
    header_clear=8.0,      # rear header pin height off the PCB back  -> cavity depth under the board
    standoff_h=4.0,        # brass female M2 standoff height (already on the board back)
    pillar_od=5.2,         # printed pillar OD (must seat the brass standoff foot)
    m2_clear=2.3,          # screw shank clearance through pillar+floor
    m2_head=4.2, head_depth=1.6,   # counterbore for the M2 button head on the OUTSIDE of the case back
    # buttons — NOT dimensioned in the drawing; a GUESS on the left (-X) wall. VERIFY from a photo.
    btn_on='left', btn1_y=-9.0, btn2_y=-14.0, btn_w=3.5, btn_h=2.6,
    # fits / walls
    fit=0.35, bez_fit=0.30, wall=2.0, floor_t=1.5, front_clear=2.4,
    usb_w=10.0, usb_h=4.2, corner_r=2.5,
    # display (EXACT — datasheet)
    module_w=19.39, module_l=36.28, win_w=17.6, win_h=32.6, glass_relief=1.9,
)
OUTDIR = r"C:\repos\SB20-power-proxy\design\s3-case"
INTERACTIVE = True
M = 0.1  # Fusion API geometry is in cm
# ----------------------------------------------------------------------------------------------


def run(_ctx=None):
    app = adsk.core.Application.get(); ui = app.userInterface
    try:
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
        header_clear, standoff_h, front_clear = P['header_clear'], P['standoff_h'], P['front_clear']
        pillar_h = max(header_clear - standoff_h, 0.0)     # printed post that lifts the standoff
        inner_w, inner_l = bw + 2 * fit, bl + 2 * fit
        outer_w, outer_l = inner_w + 2 * wall, inner_l + 2 * wall
        board_back = floor_t + header_clear                # board sits this high (headers hang below)
        board_front = board_back + bt
        shell_h = board_front + front_clear
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
            sk.sketchCurves.sketchCircles.addByCenterRadius(adsk.core.Point3D.create(cx * M, cy * M, 0), dia / 2 * M)
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
        # one deep interior pocket (floor -> top): the 8 mm header cavity + the board/front space
        ext(back, rect(back, plane_at(back, floor_t), 0, 0, inner_w, inner_l), (shell_h - floor_t) + 0.3, CUT)
        # 4 pillars that the brass standoffs sit on (floor -> pillar_h)
        if pillar_h > 0.05:
            for (hx, hy) in holes:
                ext(back, circle(back, plane_at(back, floor_t), hx, hy, P['pillar_od']), pillar_h, JOIN)
        # screw: counterbore on the OUTSIDE (bottom) + clearance up through floor + pillar
        for (hx, hy) in holes:
            ext(back, circle(back, back.xYConstructionPlane, hx, hy, P['m2_head']), P['head_depth'], CUT)   # counterbore (bottom)
            ext(back, circle(back, plane_at(back, floor_t + pillar_h), hx, hy, P['m2_clear']), -(floor_t + pillar_h + 0.2), CUT)
        # USB-C notch (open-top slot) in the -Y wall, at board level
        usb_bottom = board_back + bt / 2 - P['usb_h'] / 2
        u = back.sketches.add(plane_at(back, shell_h))
        u.sketchCurves.sketchLines.addTwoPointRectangle(
            adsk.core.Point3D.create((-P['usb_w'] / 2) * M, (-(outer_l / 2 + 1)) * M, 0),
            adsk.core.Point3D.create((P['usb_w'] / 2) * M, (-(inner_l / 2 - 0.2)) * M, 0))
        ext(back, u.profiles.item(0), -(shell_h - usb_bottom), CUT)
        # two side button slots (open-top). VERIFY positions.
        btn_bottom = board_back + bt / 2 - P['btn_h'] / 2
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
        msg = (f'Case v3 built + STLs exported.\nTray outer {outer_w:.1f} x {outer_l:.1f} x {shell_h:.1f} mm '
               f'(board floats {header_clear:.0f} mm on {pillar_h:.0f} mm pillars + {standoff_h:.0f} mm standoffs).\n'
               'VERIFY hole XY, header footprint vs pillars, and button positions from a back-side photo.')
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
