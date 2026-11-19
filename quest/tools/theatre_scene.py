"""Velaria dome theatre: a modern unidirectional dome theatre rendered as stereo equirect backgrounds, one per seat.

Run inside Blender (GUI or `blender -b -P theatre_scene.py -- <outdir> [seats...]`). Builds the room from scratch in a
new scene, saves theatre.blend beside the output, then renders seat0..seat4 as SIZE x SIZE top/bottom stereo JPEGs
(left eye on top), which the headset shows behind the dome picture in the Dome projection.

Layout (Nelson, 19 Nov): raked rows in the rear half of the room, everyone facing the front, open floor in front,
side-aisle stairs, a rail along the front row, operator console on the rear platform. Nobody sits near the front
wall: the front springline is 9 to 15 m from any seat.

Depth (Nelson, 19 Nov): the dome and the drum wall are at infinity, so they never shift against the dome picture
(which the headset also draws at infinity); only the near set (floor, tiers, seats, rails, console) carries stereo,
with a wider than natural eye spacing (IOD) so the room reads big. Infinity is done by scaling the far set 1000x
about each seat's eye point before its render: every direction is preserved, the disparity goes to zero.

Geometry (metres): dome diameter 16, springline 3.0 above the front floor, 7 rows 0.85 apart rising 0.26 per row,
seated eyes 1.2 above the row floor. Front is -Y in Blender. The five camera seats are real seats (their own chair is
left out): sweet spot (row 3 centre), front row centre, rear row centre, and the end seats of row 4. The headset's
seat table (quest/src/main.cpp kDomeSeats) is printed by this script: --table.
"""
import math, os, sys
try:
    import bpy, bmesh
    from mathutils import Matrix, Vector
except ImportError:   # --table works from a plain python
    bpy = None

R = 8.0             # dome radius
SPRING = 3.0        # springline height above the front floor
EYE = 1.2           # seated eye height above the row floor
IOD = 0.090         # eye spacing for the near set (natural 0.064; wider reads bigger)
SIZE = 6144         # output: SIZE x SIZE top/bottom, i.e. SIZE x SIZE/2 per eye
SAMPLES = 80
FAR_SCALE = 1000.0  # the far set (dome, drum wall, cove) scaled about the eye: "at infinity"

ROWS = 7
ROW_Y0 = 0.9        # first row's centre line behind the dome centre
ROW_PITCH = 0.85
RISE = 0.26         # per row
ARC_Y = -3.0        # rows curve about this point ahead of the centre
SEAT_PITCH = 0.56   # along the row
FAN_DEG = 34.0      # block half-angle about the arc centre
WALL_CLEAR = 1.4    # seats stay this far from the drum wall (the side aisles live there)
AISLE_W = 1.05      # side aisle (stairs) width, measured along the arc


def row_y(i):
    return ROW_Y0 + i * ROW_PITCH


def row_z(i):
    return i * RISE


def row_rho(i):
    return row_y(i) - ARC_Y


def seats_in_row(i):
    """(x, y, facing) for each seat of row i; the arc is centred on (0, ARC_Y)."""
    rho = row_rho(i)
    step = SEAT_PITCH / rho
    k = int(math.radians(FAN_DEG) / step)
    out = []
    for j in range(-k, k + 1):
        a = j * step
        x, y = rho * math.sin(a), ARC_Y + rho * math.cos(a)
        if math.hypot(x, y) > R - WALL_CLEAR:
            continue
        out.append((x, y, a))
    return out


def camera_seats():
    """The five headset seats as (x, y, z_eye) in Blender space: sweet spot, front, rear, left, right."""
    def centre(i):
        return (0.0, row_y(i), row_z(i) + EYE)
    r4 = seats_in_row(3)
    # the viewer's left, facing -Y, is +X in Blender (right = forward x up = -Y x +Z = -X)
    left = max(r4, key=lambda s: s[0])
    right = min(r4, key=lambda s: s[0])
    return [centre(2), centre(0), centre(6),
            (left[0], left[1], row_z(3) + EYE), (right[0], right[1], row_z(3) + EYE)]


def headset_table():
    """Eye positions relative to the dome centre, in the headset's room frame (right, up, back), as fractions of R."""
    rows = []
    for x, y, z in camera_seats():
        rows.append((-x / R, (z - SPRING) / R, y / R))
    return rows


def mat(name, rgb, rough=0.9, emit=0.0, emit_rgb=None, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if emit > 0:
        bsdf.inputs["Emission Color"].default_value = (*(emit_rgb or rgb), 1)
        bsdf.inputs["Emission Strength"].default_value = emit
    return m


def link(obj, coll, m=None):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    coll.objects.link(obj)
    if m is not None:
        if obj.data.materials:
            obj.data.materials[0] = m
        else:
            obj.data.materials.append(m)
    return obj


def mesh_obj(name, verts, faces, coll, m):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    me.materials.append(m)
    return ob


def arc_slab(name, r0, r1, a0, a1, z0, z1, coll, m, segs=None):
    """A closed slab between two arcs about (0, ARC_Y): radii r0..r1, angles a0..a1 (radians, 0 = -Y), heights z0..z1."""
    if segs is None:
        segs = max(2, int((a1 - a0) * r1 / 0.08))
    verts, faces = [], []
    for z in (z0, z1):
        for r in (r0, r1):
            for s in range(segs + 1):
                a = a0 + (a1 - a0) * s / segs
                verts.append((r * math.sin(a), ARC_Y + r * math.cos(a), z))
    n = segs + 1
    def idx(zi, ri, s):
        return zi * 2 * n + ri * n + s
    for s in range(segs):
        faces.append([idx(1, 0, s), idx(1, 0, s + 1), idx(1, 1, s + 1), idx(1, 1, s)])   # top
        faces.append([idx(0, 1, s), idx(0, 1, s + 1), idx(0, 0, s + 1), idx(0, 0, s)])   # bottom
        faces.append([idx(0, 0, s), idx(0, 0, s + 1), idx(1, 0, s + 1), idx(1, 0, s)])   # inner riser
        faces.append([idx(0, 1, s + 1), idx(0, 1, s), idx(1, 1, s), idx(1, 1, s + 1)])   # outer
    faces.append([idx(0, 0, 0), idx(1, 0, 0), idx(1, 1, 0), idx(0, 1, 0)])
    faces.append([idx(0, 1, segs), idx(1, 1, segs), idx(1, 0, segs), idx(0, 0, segs)])
    return mesh_obj(name, verts, faces, coll, m)


def cut_verts(ob, pred):
    """Delete the vertices (and what they hold up) whose local coordinate satisfies pred, in place."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    dead = [v for v in bm.verts if pred(v.co)]
    bmesh.ops.delete(bm, geom=dead, context='VERTS')
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()


def box(name, loc, scale, coll, m, rot_z=0.0, rot_x=0.0, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    ob = bpy.context.object
    ob.name = name
    ob.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    ob.rotation_euler = (rot_x, 0, rot_z)
    link(ob, coll, m)
    if bevel > 0:
        bm = ob.modifiers.new("Bevel", 'BEVEL')
        bm.width = bevel
        bm.segments = 3
        bm.limit_method = 'NONE'
    return ob


def cyl(name, loc, radius, depth, coll, m, rot=(0, 0, 0), verts=24):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=loc, rotation=rot)
    return link(bpy.context.object, coll, m)


def chair(x, y, z, a, coll, mats):
    """One theatre chair on floor z, facing the arc centre (angle a about it). Smooth-shaded, bevelled boxes."""
    rot = math.pi - a   # rotating local +Y by this about Z gives the facing direction below
    fx, fy = -math.sin(a), -math.cos(a)   # facing direction (towards the arc centre)
    m_seat, m_frame, m_arm = mats
    # pedestal, cushion, back (reclined), two armrests
    box("Ped", (x, y, z + 0.18), (0.40, 0.30, 0.36), coll, m_frame, rot)
    box("Cushion", (x, y, z + 0.44), (0.50, 0.48, 0.10), coll, m_seat, rot, bevel=0.03)
    bx, by = x - fx * 0.26, y - fy * 0.26
    box("Back", (bx, by, z + 0.72), (0.50, 0.08, 0.60), coll, m_seat, rot, rot_x=math.radians(18), bevel=0.03)   # top ~1.0 above the row floor: the row in front never blocks the screen
    for side in (-1, 1):
        ax, ay = x + side * (-fy) * 0.29, y + side * fx * 0.29
        box("Arm", (ax - fx * 0.04, ay - fy * 0.04, z + 0.64), (0.06, 0.42, 0.05), coll, m_arm, rot, bevel=0.015)
        box("ArmPost", (ax - fx * 0.18, ay - fy * 0.18, z + 0.50), (0.05, 0.06, 0.28), coll, m_frame, rot)


def build():
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    far = bpy.data.collections.new("Far")    # scaled to infinity per seat
    near = bpy.data.collections.new("Near")  # carries the stereo
    sc.collection.children.link(far)
    sc.collection.children.link(near)

    m_floor = mat("Carpet", (0.045, 0.040, 0.050), 0.95)
    m_tier = mat("Tier", (0.055, 0.050, 0.060), 0.95)
    m_nose = mat("StepNose", (0.22, 0.20, 0.18), 0.6)
    m_wall = mat("Wall", (0.07, 0.065, 0.065), 0.9)
    m_dome = mat("Dome", (0.004, 0.004, 0.005), 1.0)
    m_seat = mat("SeatFabric", (0.07, 0.09, 0.16), 0.85)
    m_frame = mat("SeatFrame", (0.015, 0.015, 0.016), 0.5, metal=0.4)
    m_arm = mat("Armrest", (0.09, 0.07, 0.05), 0.55)
    m_rail = mat("Rail", (0.45, 0.45, 0.47), 0.3, metal=1.0)
    m_console = mat("Console", (0.035, 0.035, 0.04), 0.6)
    m_screen = mat("Screen", (0.2, 0.3, 0.5), 0.4, emit=2.5, emit_rgb=(0.25, 0.40, 0.70))
    m_cove = mat("Cove", (1, 0.75, 0.5), 0.5, emit=5.0, emit_rgb=(1.0, 0.70, 0.42))
    m_step = mat("StepLight", (0.3, 0.6, 1.0), 0.5, emit=8.0, emit_rgb=(0.30, 0.50, 1.0))
    m_exit = mat("Exit", (0.2, 1.0, 0.4), 0.5, emit=4.0, emit_rgb=(0.15, 1.0, 0.35))
    m_door = mat("Door", (0.05, 0.045, 0.04), 0.7)

    # ---- far set: floor-level disc is near; the drum wall, dome and cove are far
    bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=R + 0.05, depth=SPRING + 0.4, location=(0, 0, SPRING / 2 - 0.2), end_fill_type='NOTHING')
    link(bpy.context.object, far, m_wall).name = "Wall"
    bpy.ops.mesh.primitive_uv_sphere_add(segments=128, ring_count=64, radius=R, location=(0, 0, SPRING))
    dome = link(bpy.context.object, far, m_dome)
    dome.name = "Dome"
    cut_verts(dome, lambda co: co.z < -0.01)
    bpy.ops.mesh.primitive_torus_add(major_segments=128, minor_segments=10, major_radius=R - 0.16, minor_radius=0.045, location=(0, 0, SPRING - 0.24))
    link(bpy.context.object, far, m_cove).name = "Cove"
    bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=R - 0.03, depth=0.14, location=(0, 0, SPRING - 0.11), end_fill_type='NOTHING')
    link(bpy.context.object, far, m_wall).name = "CoveLip"
    # exit doors low on the side walls, towards the back, with a small green sign
    for side in (-1, 1):
        ang = math.radians(side * 62)             # from the back (+Y)
        dx, dy = (R - 0.02) * math.sin(ang), (R - 0.02) * math.cos(ang)
        rz = -ang
        box("Door", (dx, dy, 0.55 + 1.05), (1.8, 0.06, 2.1), far, m_door, rz)
        box("ExitSign", (dx * 0.995, dy * 0.995, 0.55 + 2.3), (0.36, 0.04, 0.14), far, m_exit, rz)

    # ---- near set
    bpy.ops.mesh.primitive_circle_add(vertices=128, radius=R + 0.2, fill_type='NGON', location=(0, 0, -0.002))
    link(bpy.context.object, near, m_floor).name = "Floor"
    fan = math.radians(FAN_DEG)
    cams = camera_seats()
    n = 0
    for i in range(ROWS):
        rho = row_rho(i)
        z = row_z(i)
        # the row's tier: the seat block, then (outside the stairs) a ledge; the last row keeps a short walkway behind
        aisle = AISLE_W / rho
        aa0 = fan + 0.02 / rho          # inner edge of the stairs
        aa1 = aa0 + aisle               # outer edge of the stairs
        a_out = aa1 + 0.30 / rho        # ledge beyond the stairs
        r0, r1 = rho - 0.45, (rho + 0.40 if i < ROWS - 1 else rho + 0.90)
        if i > 0:
            arc_slab("Tier%d" % i, r0, r1, -aa0, aa0, 0.0, z, near, m_tier)
            arc_slab("LedgeL%d" % i, r0, r1, aa1, a_out, 0.0, z, near, m_tier)
            arc_slab("LedgeR%d" % i, r0, r1, -a_out, -aa1, 0.0, z, near, m_tier)
            # side-aisle stairs: two steps per row, each half a tier deep, nosing strip and a light under it
            for side in (-1, 1):
                for s in range(2):
                    sr0 = r0 + s * 0.425
                    sz = row_z(i - 1) + (s + 1) * RISE / 2
                    a0, a1 = (aa0, aa1) if side > 0 else (-aa1, -aa0)
                    arc_slab("Step%d_%d_%d" % (i, side, s), sr0, sr0 + 0.425, a0, a1, 0.0, sz, near, m_tier)
                    arc_slab("Nose%d_%d_%d" % (i, side, s), sr0, sr0 + 0.025, a0, a1, sz - 0.01, sz + 0.004, near, m_nose)
                    arc_slab("StepLight%d_%d_%d" % (i, side, s), sr0 - 0.002, sr0 + 0.01, a0 + 0.03 / rho, a1 - 0.03 / rho, sz - 0.05, sz - 0.03, near, m_step)
        for (x, y, a) in seats_in_row(i):
            if any((x - cx) ** 2 + (y - cy) ** 2 < 0.2 ** 2 for cx, cy, _ in cams):
                continue   # the camera's own chair
            chair(x, y, z, a, near, (m_seat, m_frame, m_arm))
            n += 1
    # the front rail: posts and two tubes along the arc just ahead of the first row
    rr = row_rho(0) - 0.72
    span = fan + AISLE_W / rr + 0.25 / rr
    for h, rad in ((0.92, 0.03), (0.55, 0.018)):
        bpy.ops.mesh.primitive_torus_add(major_segments=160, minor_segments=10, major_radius=rr, minor_radius=rad, location=(0, ARC_Y, h))
        t = link(bpy.context.object, near, m_rail)
        t.name = "Rail"
        # keep only the span in front of the block: cut by deleting far vertices
        cut_verts(t, lambda co: abs(math.atan2(co.x, co.y)) > span)
    k = int(span * rr / 1.1)
    for j in range(-k, k + 1):
        a = j * span / max(1, k)
        cyl("Post", (rr * math.sin(a), ARC_Y + rr * math.cos(a), 0.46), 0.025, 0.92, near, m_rail)
    # presenter's lectern on the open floor ahead of the block (reference photos), one dim screen
    box("Lectern", (2.3, -0.9, 0.55), (0.8, 0.55, 1.1), near, m_console, math.radians(-15), bevel=0.02)
    box("Screen", (2.3, -0.9, 1.14), (0.5, 0.03, 0.3), near, m_screen, math.radians(-15), math.radians(-55))
    # world: black; a soft neutral fill from above so the seats read in the dark
    sc.world = bpy.data.worlds.new("World")
    sc.world.use_nodes = True
    sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0, 0, 0, 1)
    bpy.ops.object.light_add(type='AREA', location=(0, 2.0, SPRING + 2.5))
    fill = bpy.context.object
    fill.data.energy = 260
    fill.data.size = 9
    fill.data.color = (1.0, 0.9, 0.8)
    link(fill, near)
    # camera: stereo equirect; image centre = -Y = the front
    bpy.ops.object.camera_add(location=(0, 0, EYE), rotation=(math.radians(90), 0, math.radians(180)))
    cam = bpy.context.object
    cam.name = "SeatCam"
    cam.data.type = 'PANO'
    try:
        cam.data.panorama_type = 'EQUIRECTANGULAR'
    except Exception:
        cam.data.cycles.panorama_type = 'EQUIRECTANGULAR'
    cam.data.clip_end = 100000.0
    cam.data.stereo.convergence_mode = 'PARALLEL'
    cam.data.stereo.interocular_distance = IOD
    cam.data.stereo.use_spherical_stereo = True
    cam.data.stereo.use_pole_merge = True
    cam.data.stereo.pole_merge_angle_from = math.radians(60)
    cam.data.stereo.pole_merge_angle_to = math.radians(75)
    sc.camera = cam
    link(cam, near)
    # render settings
    sc.render.engine = 'CYCLES'
    sc.cycles.samples = SAMPLES
    sc.cycles.use_denoising = True
    sc.cycles.device = 'GPU'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type = 'METAL'
        for d in prefs.get_devices_for_type('METAL'):
            d.use = True
    except Exception:
        pass
    sc.render.resolution_x = SIZE
    sc.render.resolution_y = SIZE // 2
    sc.render.resolution_percentage = 100
    sc.render.use_multiview = True
    sc.render.views_format = 'STEREO_3D'
    sc.render.image_settings.file_format = 'JPEG'
    sc.render.image_settings.quality = 92
    sc.render.image_settings.views_format = 'STEREO_3D'
    sc.render.image_settings.stereo_3d_format.display_mode = 'TOPBOTTOM'
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.exposure = 0.5
    bpy.ops.object.select_all(action='DESELECT')
    for ob in near.objects:
        if ob.type == 'MESH' and ob.name.startswith(("Cushion", "Back", "Arm", "Post", "Rail", "Ped", "Lectern")):
            ob.select_set(True)
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(40))
    except Exception:
        try:
            bpy.ops.object.shade_auto_smooth(angle=math.radians(40))
        except Exception:
            pass
    bpy.ops.object.select_all(action='DESELECT')
    return cam, n


def place_far(far, eye, base):
    """Scale the far set FAR_SCALE times about the eye point: same directions, no disparity, no parallax."""
    e = Vector(eye)
    M = Matrix.Translation(e) @ Matrix.Scale(FAR_SCALE, 4) @ Matrix.Translation(-e)
    for ob in far.objects:
        ob.matrix_world = M @ base[ob.name]


def render(cam, outdir, seats):
    sc = bpy.context.scene
    far = bpy.data.collections["Far"]
    base = {ob.name: ob.matrix_world.copy() for ob in far.objects}
    cams = camera_seats()
    for i in seats:
        x, y, z = cams[i]
        cam.location = (x, y, z)
        place_far(far, (x, y, z), base)
        sc.render.filepath = os.path.join(outdir, "seat%d.jpg" % i)
        bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if args and args[0] == "--table":
        names = ["sweet spot (row 3)", "front row", "rear row", "left end of row 4", "right end of row 4"]
        print("// seats: eye position from the dome centre (right, up, back) / R; theatre_scene.py --table")
        for (rx, up, bk), nm in zip(headset_table(), names):
            print("    {%.4ff, %.4ff, %.4ff},   // %s" % (rx, up, bk, nm))
        sys.exit(0)
    kv = dict(a.split("=", 1) for a in args if "=" in a)
    args = [a for a in args if "=" not in a]
    SIZE = int(kv.get("size", SIZE))
    SAMPLES = int(kv.get("samples", SAMPLES))
    outdir = args[0] if args else os.path.expanduser("~/VRLink/xfer/theatre")
    seats = [int(a) for a in args[1:]] if len(args) > 1 else [0, 1, 2, 3, 4]
    os.makedirs(outdir, exist_ok=True)
    cam, n = build()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(outdir, "theatre.blend"))
    print("theatre: %d seats modelled" % n)
    for (rx, up, bk) in headset_table():
        print("seat table: %.4f %.4f %.4f" % (rx, up, bk))
    if bpy.app.background:
        render(cam, outdir, seats)
