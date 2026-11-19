"""Velaria dome theatre: parametric dome theatres rendered as stereo equirect backgrounds, one per seat per venue.

Run inside Blender (`blender -b -P theatre_scene.py -- <outdir> [venues] [seats] [size=N] [samples=N]`):
    venues   e.g. M0 L30 (size letter + tilt degrees); default: all twelve (S/M/L x 0/15/25/30)
    seats    0..4 (default all five); `preview` renders one 1600 x 900 perspective still per venue instead
    --tables [path]  prints (or writes) quest/src/dome_seats.h, the headset's eye-position tables; no Blender needed

Venues (Nelson, 19 Nov, after his reference photos and the IPS / Domerama / flying-cinema figures):
    Small 12 m (R 6)   planetarium or hybrid        Medium 18 m (R 9)        Large 26 m (R 13, MOSI class)
    Tilt: Level (classic planetarium), 15 (hybrid), 25 and 30 (large-format museum cinema, IMAX Dome style).
All rooms are unidirectional: raked rows in the rear half, everyone facing the front, side-aisle stairs with step
lights, a rail ahead of row 1, open floor and a lectern in front. Tilted rooms rake steeper (17 + 0.45 x tilt
degrees), the chairs recline further (18 + 0.4 x tilt), and the screen wall runs down to the floor ahead of the
audience. Row pitch 1.0 m, seat pitch 0.56 m, seated eyes 1.2 m above the row floor and 12 cm behind the cushion.

Depth: the dome and the skirt wall are at infinity (the far set is scaled 1000x about each seat's eye before its
render: directions exact, disparity nil), so they never shift against the dome picture, which the headset also
draws at infinity. Floor, tiers, seats, rail carry the stereo at a natural 64 mm.

Light (Nelson: "top lit as if from the screen... deep shadows"): one soft cool source from the dome above-front
standing in for the picture, step lights and exit signs, nothing else. No cove light: it is off during a show.

Frames: Blender front is -Y; the headset's room frame is (right, up, back) = (-x, z - hC, y) / R.
"""
import math, os, sys
try:
    import bpy, bmesh
    from mathutils import Matrix, Vector
except ImportError:   # --tables works from a plain python
    bpy = None

SIZES = {"S": 6.0, "M": 9.0, "L": 13.0}      # dome radius (m)
TILTS = [0, 15, 25, 30]                       # degrees the dome's front edge drops below level
SEAT_NAMES = ["sweet spot", "front row", "rear row", "left end", "right end"]

EYE = 1.2            # seated eye height above the row floor
EYE_BACK = 0.12      # eyes behind the cushion's centre
IOD = 0.064          # natural eye spacing (90 mm read cross-eyed on the neighbour chair, 19 Nov)
SIZE = 6144          # output: SIZE x SIZE top/bottom, i.e. SIZE x SIZE/2 per eye
SAMPLES = 80
FAR_SCALE = 1000.0

ROW_PITCH = 1.0
SEAT_PITCH = 0.56
FAN_DEG = 34.0       # block half-angle about the arc centre
WALL_CLEAR = 1.4     # seats stay this far from the wall (the side aisles live there)
AISLE_W = 1.05
HEAD_CLEAR = 1.8     # a row is dropped when the dome comes closer than this above seated eyes


class Venue:
    def __init__(self, size, tilt):
        self.size, self.tilt = size, tilt
        self.R = SIZES[size]
        R, t = self.R, math.radians(tilt)
        self.t = t
        # the dome's centre above the front floor: a drum for a level dome; a tilted dome's front edge comes down to
        # 1.2 m above the front floor, the way an IMAX Dome's screen wall meets the floor ahead of the audience
        drum = max(2.6, 0.37 * R)
        self.hC = drum if tilt == 0 else max(drum * math.cos(t), 1.2 + R * math.sin(t))
        self.arc_y = -0.375 * R                   # rows curve about this point ahead of the centre
        self.rake = math.radians(17 + 0.45 * tilt)
        self.rise = ROW_PITCH * math.tan(self.rake)
        self.recline = math.radians(18 + 0.4 * tilt)
        y0 = 0.12 * R
        rows = []
        i = 0
        while True:
            y = y0 + i * ROW_PITCH
            z = i * self.rise
            if y > 0.86 * R or not self.inside_footprint(0.0, y, 2.0):
                break
            if self.dome_height_above(0.0, y) - (z + EYE) < HEAD_CLEAR:
                break
            rows.append((y, z))
            i += 1
        self.rows = rows

    def inside_footprint(self, x, y, margin=0.0):
        """Inside the rim's plan (an ellipse R x R cos t for a tilted dome), `margin` metres in from the wall."""
        a = self.R - margin
        b = a * math.cos(self.t)
        return (x / a) ** 2 + (y / b) ** 2 <= 1.0

    # ---- dome geometry: a hemisphere of radius R about C = (0, 0, hC), rotated by t about X (front edge down)
    def to_local(self, p):
        x, y, z = p[0], p[1], p[2] - self.hC
        c, s = math.cos(-self.t), math.sin(-self.t)
        return (x, y * c - z * s, y * s + z * c)

    def to_world(self, p):
        c, s = math.cos(self.t), math.sin(self.t)
        return (p[0], p[1] * c - p[2] * s, self.hC + p[1] * s + p[2] * c)

    def dome_height_above(self, x, y):
        """World z of the dome surface straight above (x, y), or a big number if no dome is overhead."""
        R = self.R
        def inside(z):
            l = self.to_local((x, y, z))
            return l[0] ** 2 + l[1] ** 2 + l[2] ** 2 < R * R and l[2] >= 0
        lo = None
        for k in range(400):   # find a height that is under the dome
            z = -2.0 + k * 0.1
            if inside(z):
                lo = z
                break
        if lo is None:
            return 1e9
        hi = self.hC + R + 1
        for _ in range(60):
            mid = (lo + hi) / 2
            if inside(mid):
                lo = mid
            else:
                hi = mid
        return lo

    # ---- seating
    def rho(self, i):
        return self.rows[i][0] - self.arc_y

    def seats_in_row(self, i):
        rho = self.rho(i)
        step = SEAT_PITCH / rho
        k = int(math.radians(FAN_DEG) / step)
        out = []
        for j in range(-k, k + 1):
            a = j * step
            x, y = rho * math.sin(a), self.arc_y + rho * math.cos(a)
            if not self.inside_footprint(x, y, WALL_CLEAR):
                continue
            out.append((x, y, a))
        return out

    def camera_seats(self):
        """(x, y, z_eye) in Blender space: sweet spot (row ~0.6 of the block), front, rear, left and right ends of the middle row."""
        n = len(self.rows)
        def eye(i, x, y, a):
            return (x + EYE_BACK * math.sin(a), y + EYE_BACK * math.cos(a), self.rows[i][1] + EYE)
        def centre(i):
            return eye(i, 0.0, self.rows[i][0], 0.0)
        mid = n // 2
        r = self.seats_in_row(mid)
        left = max(r, key=lambda s: s[0])     # the viewer's left, facing -Y, is +X
        right = min(r, key=lambda s: s[0])
        return [centre(min(n - 1, round(0.6 * (n - 1)))), centre(0), centre(n - 1), eye(mid, *left), eye(mid, *right)]

    def headset_table(self):
        return [(-x / self.R, (z - self.hC) / self.R, y / self.R) for x, y, z in self.camera_seats()]

    @property
    def tag(self):
        return "%s%d" % (self.size, self.tilt)


def all_venues():
    return [Venue(s, t) for s in SIZES for t in TILTS]


def write_tables(path=None):
    lines = ["// Generated by quest/tools/theatre_scene.py --tables. Eye positions from the dome's centre, (right, up,",
             "// back) as fractions of the dome radius, for each venue size, tilt and seat. Do not edit by hand.",
             "#pragma once", "",
             "constexpr int kVenueSizes = %d, kVenueTilts = %d, kVenueSeats = 5;" % (len(SIZES), len(TILTS)),
             "static const char* const kVenueSizeTag[kVenueSizes] = {%s};" % ", ".join('"%s"' % s for s in SIZES),
             "static const float kVenueDiameter[kVenueSizes] = {%s};" % ", ".join("%.1ff" % (2 * r) for r in SIZES.values()),
             "static const int kVenueTilt[kVenueTilts] = {%s};" % ", ".join(str(t) for t in TILTS),
             "static const float kDomeSeats[kVenueSizes][kVenueTilts][kVenueSeats][3] = {"]
    for s in SIZES:
        lines.append("    {   // %s, %.0f m" % (s, 2 * SIZES[s]))
        for t in TILTS:
            v = Venue(s, t)
            cells = ", ".join("{%.4ff, %.4ff, %.4ff}" % row for row in v.headset_table())
            lines.append("        {%s},   // tilt %d, %d rows" % (cells, t, len(v.rows)))
        lines.append("    },")
    lines.append("};")
    text = "\n".join(lines) + "\n"
    if path:
        open(path, "w").write(text)
    return text


# ------------------------------------------------------------------------------------------------ Blender

def mat(name, rgb, rough=0.9, emit=0.0, emit_rgb=None, metal=0.0, specular=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if specular is not None:
        for key in ("Specular IOR Level", "Specular"):
            if key in bsdf.inputs:
                bsdf.inputs[key].default_value = specular
                break
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


def arc_slab(name, cy, r0, r1, a0, a1, z0, z1, coll, m, segs=None):
    """A closed slab between two arcs about (0, cy): radii r0..r1, angles a0..a1 (radians, 0 = -Y), heights z0..z1."""
    if segs is None:
        segs = max(2, int((a1 - a0) * r1 / 0.08))
    verts, faces = [], []
    for z in (z0, z1):
        for r in (r0, r1):
            for s in range(segs + 1):
                a = a0 + (a1 - a0) * s / segs
                verts.append((r * math.sin(a), cy + r * math.cos(a), z))
    n = segs + 1
    def idx(zi, ri, s):
        return zi * 2 * n + ri * n + s
    for s in range(segs):
        faces.append([idx(1, 0, s), idx(1, 0, s + 1), idx(1, 1, s + 1), idx(1, 1, s)])
        faces.append([idx(0, 1, s), idx(0, 1, s + 1), idx(0, 0, s + 1), idx(0, 0, s)])
        faces.append([idx(0, 0, s), idx(0, 0, s + 1), idx(1, 0, s + 1), idx(1, 0, s)])
        faces.append([idx(0, 1, s + 1), idx(0, 1, s), idx(1, 1, s), idx(1, 1, s + 1)])
    faces.append([idx(0, 0, 0), idx(1, 0, 0), idx(1, 1, 0), idx(0, 1, 0)])
    faces.append([idx(0, 1, segs), idx(1, 1, segs), idx(1, 0, segs), idx(0, 0, segs)])
    return mesh_obj(name, verts, faces, coll, m)


def cut_verts(ob, pred):
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


def chair(x, y, z, a, recline, coll, mats):
    """One chair; returns (parts, back) so a render can hide a chair or just its backrest."""
    rot = math.pi - a
    fx, fy = -math.sin(a), -math.cos(a)
    m_seat, m_frame, m_arm = mats
    parts = [box("Ped", (x, y, z + 0.18), (0.40, 0.30, 0.36), coll, m_frame, rot),
             box("Cushion", (x, y, z + 0.44), (0.50, 0.48, 0.10), coll, m_seat, rot, bevel=0.03)]
    bx, by = x - fx * 0.26, y - fy * 0.26
    back = box("Back", (bx, by, z + 0.72), (0.50, 0.08, 0.60), coll, m_seat, rot, rot_x=recline, bevel=0.03)
    parts.append(back)
    for side in (-1, 1):
        ax, ay = x + side * (-fy) * 0.29, y + side * fx * 0.29
        parts.append(box("Arm", (ax - fx * 0.04, ay - fy * 0.04, z + 0.64), (0.06, 0.42, 0.05), coll, m_arm, rot, bevel=0.015))
        parts.append(box("ArmPost", (ax - fx * 0.18, ay - fy * 0.18, z + 0.50), (0.05, 0.06, 0.28), coll, m_frame, rot))
    return parts, back


def build(v):
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    far = bpy.data.collections.new("Far")
    near = bpy.data.collections.new("Near")
    sc.collection.children.link(far)
    sc.collection.children.link(near)
    R, hC, t = v.R, v.hC, v.t

    m_floor = mat("Carpet", (0.045, 0.040, 0.050), 0.95)
    m_tier = mat("Tier", (0.055, 0.050, 0.060), 0.95)
    m_nose = mat("StepNose", (0.22, 0.20, 0.18), 0.6)
    m_wall = mat("Wall", (0.05, 0.047, 0.047), 0.9)
    m_dome = mat("Dome", (0.0, 0.0, 0.0), 1.0, specular=0.0)   # the picture goes here: black, reflects nothing
    m_seat = mat("SeatFabric", (0.07, 0.09, 0.16), 0.85)
    m_frame = mat("SeatFrame", (0.015, 0.015, 0.016), 0.5, metal=0.4)
    m_arm = mat("Armrest", (0.09, 0.07, 0.05), 0.55)
    m_rail = mat("Rail", (0.45, 0.45, 0.47), 0.3, metal=1.0)
    m_console = mat("Console", (0.035, 0.035, 0.04), 0.6)
    m_screen = mat("Screen", (0.2, 0.3, 0.5), 0.4, emit=0.4, emit_rgb=(0.25, 0.40, 0.70))
    m_step = mat("StepLight", (0.3, 0.6, 1.0), 0.5, emit=1.0, emit_rgb=(0.30, 0.50, 1.0))
    m_exit = mat("Exit", (0.2, 1.0, 0.4), 0.5, emit=0.9, emit_rgb=(0.15, 1.0, 0.35))
    m_door = mat("Door", (0.05, 0.045, 0.04), 0.7)

    # ---- far set: the dome (black; the headset draws the picture there) and the skirt wall under its rim
    bpy.ops.mesh.primitive_uv_sphere_add(segments=160, ring_count=80, radius=R, location=(0, 0, 0))
    dome = link(bpy.context.object, far, m_dome)
    dome.name = "Dome"
    cut_verts(dome, lambda co: co.z < -0.01)
    dome.rotation_euler = (t, 0, 0)
    dome.location = (0, 0, hC)
    segs = 192
    rim = [v.to_world((R * math.sin(2 * math.pi * i / segs), R * math.cos(2 * math.pi * i / segs), 0.0)) for i in range(segs)]
    verts = [(p[0], p[1], p[2] + 0.02) for p in rim] + [(p[0], p[1], -0.3) for p in rim]
    faces = [[i, (i + 1) % segs, segs + (i + 1) % segs, segs + i] for i in range(segs)]
    mesh_obj("Skirt", verts, faces, far, m_wall)
    # exit doors at the rear corners (on the skirt, at the rear platform's height)
    zr = v.rows[-1][1] if v.rows else 0.0
    for side in (-1, 1):
        ang = math.radians(side * 38)
        dx, dy = (R - 0.04) * math.sin(ang), (R - 0.04) * math.cos(ang) * math.cos(t)
        box("Door", (dx, dy, zr + 1.05), (1.8, 0.06, 2.1), far, m_door, -ang)
        box("ExitSign", (dx * 0.99, dy * 0.99, zr + 2.3), (0.36, 0.04, 0.14), far, m_exit, -ang)

    # ---- near set
    bpy.ops.mesh.primitive_circle_add(vertices=160, radius=R + 0.3, fill_type='NGON', location=(0, 0, -0.002))
    link(bpy.context.object, near, m_floor).name = "Floor"
    fan = math.radians(FAN_DEG)
    cams = v.camera_seats()
    own_back, neighbours = {}, {}
    n = 0
    nrows = len(v.rows)
    steps_per_row = max(2, int(math.ceil(v.rise / 0.18)))
    for i, (ry, z) in enumerate(v.rows):
        rho = v.rho(i)
        aisle = AISLE_W / rho
        aa0 = fan + 0.02 / rho
        aa1 = aa0 + aisle
        a_out = aa1 + 0.30 / rho
        r0, r1 = rho - 0.5, (rho + 0.5 if i < nrows - 1 else rho + 0.9)
        if i > 0:
            arc_slab("Tier%d" % i, v.arc_y, r0, r1, -aa0, aa0, 0.0, z, near, m_tier)
            arc_slab("LedgeL%d" % i, v.arc_y, r0, r1, aa1, a_out, 0.0, z, near, m_tier)
            arc_slab("LedgeR%d" % i, v.arc_y, r0, r1, -a_out, -aa1, 0.0, z, near, m_tier)
            zprev = v.rows[i - 1][1]
            depth = ROW_PITCH / steps_per_row
            for side in (-1, 1):
                for s in range(steps_per_row):
                    sr0 = r0 + s * depth
                    sz = zprev + (s + 1) * (z - zprev) / steps_per_row
                    a0, a1 = (aa0, aa1) if side > 0 else (-aa1, -aa0)
                    arc_slab("Step%d_%d_%d" % (i, side, s), v.arc_y, sr0, sr0 + depth, a0, a1, 0.0, sz, near, m_tier)
                    arc_slab("Nose%d_%d_%d" % (i, side, s), v.arc_y, sr0, sr0 + 0.025, a0, a1, sz - 0.01, sz + 0.004, near, m_nose)
                    arc_slab("StepLight%d_%d_%d" % (i, side, s), v.arc_y, sr0 - 0.002, sr0 + 0.01, a0 + 0.03 / rho, a1 - 0.03 / rho, sz - 0.05, sz - 0.03, near, m_step)
        for (x, y, a) in v.seats_in_row(i):
            parts, back = chair(x, y, z, a, v.recline, near, (m_seat, m_frame, m_arm))
            n += 1
            for ci, (cx, cy, _) in enumerate(cams):
                d2 = (x - cx) ** 2 + (y - cy) ** 2
                if d2 < 0.3 ** 2:
                    own_back.setdefault(ci, []).extend(parts)         # the viewer's own chair: not drawn (distracting, eye-crossing; Nelson 12:44)
                elif d2 < 0.95 ** 2:
                    neighbours.setdefault(ci, []).extend(parts)        # the seats either side stay empty (strain at 0.6 m)
    # the front rail
    rr = v.rho(0) - 0.75
    span = fan + AISLE_W / rr + 0.25 / rr
    for h, rad in ((0.92, 0.03), (0.55, 0.018)):
        bpy.ops.mesh.primitive_torus_add(major_segments=200, minor_segments=10, major_radius=rr, minor_radius=rad, location=(0, v.arc_y, h))
        tor = link(bpy.context.object, near, m_rail)
        tor.name = "Rail"
        cut_verts(tor, lambda co: abs(math.atan2(co.x, co.y)) > span)
    k = int(span * rr / 1.1)
    for j in range(-k, k + 1):
        a = j * span / max(1, k)
        cyl("Post", (rr * math.sin(a), v.arc_y + rr * math.cos(a), 0.46), 0.025, 0.92, near, m_rail)
    # lectern on the open floor (planetarium and hybrid rooms; a tilted cinema has no presenter's spot)
    if v.tilt <= 15:
        lx, ly = 0.29 * R, -0.11 * R
        box("Lectern", (lx, ly, 0.55), (0.8, 0.55, 1.1), near, m_console, math.radians(-15), bevel=0.02)
        box("Screen", (lx, ly, 1.14), (0.5, 0.03, 0.3), near, m_screen, math.radians(-15), math.radians(-55))

    # ---- light: the show, as one soft cool source from the dome above-front; a whisper of ambient
    sc.world = bpy.data.worlds.new("World")
    sc.world.use_nodes = True
    sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.0002, 0.00025, 0.00045, 1)
    lpos = Vector(v.to_world((0.0, -0.35 * R, 0.75 * R)))
    block = Vector((0.0, (v.rows[0][0] + v.rows[-1][0]) / 2, v.rows[nrows // 2][1] + 1.0))
    bpy.ops.object.light_add(type='AREA', location=lpos)
    show = bpy.context.object
    show.data.energy = 0.4 * R * R   # "just barely able to see floors and walls" (Nelson, 19 Nov; two stops down again 12:44)
    show.data.size = 0.25 * R
    show.data.color = (0.60, 0.68, 0.85)
    show.rotation_euler = (block - lpos).to_track_quat('-Z', 'Y').to_euler()
    link(show, near)
    # and a much weaker, broader glow from the whole dome, so the shadows are deep but not empty
    zpos = Vector(v.to_world((0.0, 0.0, 0.9 * R)))
    bpy.ops.object.light_add(type='AREA', location=zpos)
    glow = bpy.context.object
    glow.data.energy = 0.075 * R * R
    glow.data.size = 0.9 * R
    glow.data.color = (0.55, 0.62, 0.80)
    glow.rotation_euler = (block - zpos).to_track_quat('-Z', 'Y').to_euler()
    link(glow, near)

    # ---- camera: stereo equirect; image centre = -Y = the front
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
    bloom()
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
    v.per_seat_hidden = {ci: own_back.get(ci, []) + neighbours.get(ci, []) for ci in range(len(cams))}
    return cam, n


def bloom():
    """A faint halation on the step lights, exit signs and the lectern screen (Nelson, 19 Nov 12:49): a compositor
    glare pass; nothing else in the room is bright enough to bloom. Blender 5.x keeps the compositor in a node
    group on scene.compositing_node_group with socket-style settings; 4.x used scene.node_tree."""
    sc = bpy.context.scene
    tree = None
    if hasattr(sc, "compositing_node_group"):
        tree = bpy.data.node_groups.new("VelariaBloom", 'CompositorNodeTree')
        sc.compositing_node_group = tree
    elif hasattr(sc, "node_tree"):
        sc.use_nodes = True
        tree = sc.node_tree
        for n in list(tree.nodes):
            tree.nodes.remove(n)
    if tree is None:
        return
    rl = tree.nodes.new('CompositorNodeRLayers')
    glare = tree.nodes.new('CompositorNodeGlare')
    settings = {"Type": ("Bloom", "BLOOM"), "Quality": ("High", "HIGH"), "Threshold": (0.35,), "Strength": (0.22,),
                "Size": (0.6,), "Saturation": (1.0,)}
    for key, vals in settings.items():
        if key not in glare.inputs:
            continue
        for val in vals:   # menu sockets take the label in 5.x ("Bloom"), the identifier elsewhere
            try:
                glare.inputs[key].default_value = val
                break
            except Exception:
                continue
    for attr, val in (("glare_type", 'BLOOM'), ("threshold", 0.35), ("mix", -0.55), ("size", 7), ("quality", 'HIGH')):
        if hasattr(glare, attr):
            try:
                setattr(glare, attr, val)
            except Exception:
                pass
    if hasattr(sc, "compositing_node_group"):
        tree.interface.new_socket(name="Image", in_out='OUTPUT', socket_type='NodeSocketColor')
        out = tree.nodes.new('NodeGroupOutput')
        tree.links.new(glare.outputs['Image'], out.inputs['Image'])
    else:
        out = tree.nodes.new('CompositorNodeComposite')
        tree.links.new(glare.outputs['Image'], out.inputs['Image'])
    tree.links.new(rl.outputs['Image'], glare.inputs['Image'])


def place_far(far, eye, base):
    e = Vector(eye)
    M = Matrix.Translation(e) @ Matrix.Scale(FAR_SCALE, 4) @ Matrix.Translation(-e)
    for ob in far.objects:
        ob.matrix_world = M @ base[ob.name]


def render_seats(v, cam, outdir, seats):
    sc = bpy.context.scene
    far = bpy.data.collections["Far"]
    base = {ob.name: ob.matrix_world.copy() for ob in far.objects}
    cams = v.camera_seats()
    for i in seats:
        x, y, z = cams[i]
        cam.location = (x, y, z)
        place_far(far, (x, y, z), base)
        for ci, objs in v.per_seat_hidden.items():
            for ob in objs:
                ob.hide_render = ci == i
        sc.render.filepath = os.path.join(outdir, "%s_seat%d.jpg" % (v.tag, i))
        bpy.ops.render.render(write_still=True)


def render_preview(v, outdir):
    """Two perspective stills per venue: from the left aisle looking across, and from the rear platform looking front."""
    sc = bpy.context.scene
    sc.render.use_multiview = False
    sc.render.resolution_x, sc.render.resolution_y = 1600, 900
    sc.cycles.samples = 48
    R = v.R
    mi = len(v.rows) * 2 // 3
    mid = v.rows[mi]
    rear = v.rows[-1]
    rho = v.rho(mi)
    a_cam = math.radians(FAN_DEG) + (AISLE_W * 0.5 + 0.1) / rho     # on the left stairs (+X), mid row
    aisle_pos = Vector((rho * math.sin(a_cam), v.arc_y + rho * math.cos(a_cam), mid[1] + 1.7))
    shots = {
        "aisle": (aisle_pos, Vector((-0.15 * R, -0.3 * R, v.hC * 0.5)), 16),
        "rear": (Vector((0.0, rear[0] + 0.6, rear[1] + 1.7)), Vector((0.0, v.rows[0][0] - 2.0, v.rows[0][1] + 0.9)), 14),
    }
    for name, (pos, aim, lens) in shots.items():
        bpy.ops.object.camera_add(location=pos)
        cam = bpy.context.object
        cam.data.lens = lens
        cam.rotation_euler = (aim - pos).to_track_quat('-Z', 'Y').to_euler()
        sc.camera = cam
        sc.render.filepath = os.path.join(outdir, "preview_%s_%s.jpg" % (v.tag, name))
        bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if args and args[0] == "--tables":
        out = args[1] if len(args) > 1 else None
        text = write_tables(out)
        if not out:
            print(text)
        for v in all_venues():
            print("%-4s R %4.1f hC %4.2f rows %2d rise %.2f" % (v.tag, v.R, v.hC, len(v.rows), v.rise))
        sys.exit(0)
    kv = dict(a.split("=", 1) for a in args if "=" in a)
    args = [a for a in args if "=" not in a]
    SIZE = int(kv.get("size", SIZE))
    SAMPLES = int(kv.get("samples", SAMPLES))
    outdir = args[0] if args else os.path.expanduser("~/VRLink/xfer/theatre")
    tags = [a for a in args[1:] if a[:1] in SIZES and a[1:].isdigit()]
    seats = [int(a) for a in args[1:] if a.isdigit()] or [0, 1, 2, 3, 4]
    preview = "preview" in args[1:]
    venues = [Venue(a[0], int(a[1:])) for a in tags] or all_venues()
    os.makedirs(outdir, exist_ok=True)
    for v in venues:
        cam, n = build(v)
        print("theatre %s: %d rows, %d chairs, dome centre %.2f m up" % (v.tag, len(v.rows), n, v.hC))
        if bpy.app.background:
            if preview:
                render_preview(v, outdir)
            else:
                render_seats(v, cam, outdir, seats)
    if len(venues) == 1:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(outdir, "theatre_%s.blend" % venues[0].tag))
