"""Velaria dome theatre: a plain planetarium interior rendered as stereo equirect backgrounds, one per seat.

Run inside Blender (GUI or `blender -b -P theatre_scene.py -- <outdir> [seats]`). Builds the room from scratch in a
new scene, saves theatre.blend beside the output, then renders seat0..seat4 as 2048 x 2048 top/bottom stereo JPEGs
(left eye on top), which the headset shows behind the dome picture in the Dome projection.

Geometry (metres, level theatre, matches the headset's defaults): dome diameter 16, springline 3.0 above the floor,
seated eyes 1.2 above the floor (so the rim is 1.8 above the eyes, the headset's domeRim default). Front is -Y.
Seats: unidirectional rows on arcs facing front, a rear console, cove light under the springline, step lights.
Cameras: centre (0,0), front (0,-4), rear (0,4), left (-4,0), right (4,0) at eye height, i.e. half a radius off
centre, the headset's kDomeSeatFrac. The dome surface itself is black: the headset draws the picture there.
"""
import bpy, math, os, sys

R = 8.0            # dome radius
SPRING = 3.0       # springline height above the floor
EYE = 1.2          # seated eye height
SEAT_R = R * 0.5   # off-centre seats
SEATS = {0: (0, 0), 1: (0, -SEAT_R), 2: (0, SEAT_R), 3: (-SEAT_R, 0), 4: (SEAT_R, 0)}


def mat(name, rgb, rough=0.9, emit=0.0, emit_rgb=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1)
    bsdf.inputs["Roughness"].default_value = rough
    if emit > 0:
        bsdf.inputs["Emission Color"].default_value = (*(emit_rgb or rgb), 1)
        bsdf.inputs["Emission Strength"].default_value = emit
    return m


def build():
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    # materials
    m_floor = mat("Floor", (0.07, 0.068, 0.075), 0.95)
    m_wall = mat("Wall", (0.10, 0.09, 0.085), 0.9)
    m_dome = mat("Dome", (0.005, 0.005, 0.006), 1.0)
    m_seat = mat("Seat", (0.08, 0.11, 0.18), 0.85)
    m_seat_frame = mat("SeatFrame", (0.02, 0.02, 0.02), 0.6)
    m_console = mat("Console", (0.04, 0.04, 0.045), 0.7)
    m_cove = mat("Cove", (1, 0.75, 0.5), 0.5, emit=4.0, emit_rgb=(1.0, 0.72, 0.45))
    m_step = mat("Step", (0.3, 0.6, 1.0), 0.5, emit=6.0, emit_rgb=(0.35, 0.55, 1.0))
    m_front = mat("FrontMark", (1, 0.2, 0.2), 0.5, emit=1.5, emit_rgb=(1, 0.15, 0.1))

    def add(obj, m):
        obj.data.materials.append(m)
        return obj

    # floor
    bpy.ops.mesh.primitive_circle_add(vertices=96, radius=R + 0.6, fill_type='NGON', location=(0, 0, 0))
    add(bpy.context.object, m_floor).name = "Floor"
    # drum wall (inside faces)
    bpy.ops.mesh.primitive_cylinder_add(vertices=96, radius=R + 0.05, depth=SPRING, location=(0, 0, SPRING / 2), end_fill_type='NOTHING')
    wall = add(bpy.context.object, m_wall)
    wall.name = "Wall"
    # dome: a hemisphere of radius R sitting on the springline (black; the headset draws the picture there)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=96, ring_count=48, radius=R, location=(0, 0, SPRING))
    dome = add(bpy.context.object, m_dome)
    dome.name = "Dome"
    # cut the lower half away
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='DESELECT')
    bpy.ops.object.mode_set(mode='OBJECT')
    for v in dome.data.vertices:
        v.select = v.co.z < -0.01
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.delete(type='VERT')
    bpy.ops.object.mode_set(mode='OBJECT')
    # cove light: a thin emissive ring just under the springline, tucked behind a lip
    bpy.ops.mesh.primitive_torus_add(major_segments=96, minor_segments=8, major_radius=R - 0.15, minor_radius=0.04, location=(0, 0, SPRING - 0.22))
    add(bpy.context.object, m_cove).name = "Cove"
    bpy.ops.mesh.primitive_cylinder_add(vertices=96, radius=R - 0.02, depth=0.12, location=(0, 0, SPRING - 0.10), end_fill_type='NOTHING')
    add(bpy.context.object, m_wall).name = "CoveLip"
    # seats: rows on arcs facing the front (-Y)
    rows = [2.2, 3.2, 4.2, 5.2, 6.2, 7.0]
    skip = [(x, y) for x, y in SEATS.values()]
    n = 0
    for ri, rr in enumerate(rows):
        half = math.radians(118 if rr > 3 else 60)   # seats wrap well past the viewer; the rear stays open for the console
        step = 0.62 / rr   # angular pitch for 0.62 m between seats
        k = int(half / step)
        for i in range(-k, k + 1):
            a = i * step
            x, y = rr * math.sin(a), -rr * math.cos(a)
            # aisle down the middle and at ±35 degrees
            if abs(a) < step * 0.6 or abs(abs(a) - math.radians(40)) < step * 0.6 or abs(abs(a) - math.radians(85)) < step * 0.6:
                continue
            if any((x - sx) ** 2 + (y - sy) ** 2 < 0.45 ** 2 for sx, sy in skip):
                continue
            # face the dome's front edge (0, -R): f is the seat's facing direction, rot aligns the box's local +Y with it
            fx, fy = 0 - x, -R - y
            fl = math.hypot(fx, fy); fx, fy = fx / fl, fy / fl
            rot = math.atan2(fy, fx) - math.pi / 2
            bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y, 0.45))
            s = add(bpy.context.object, m_seat)
            s.scale = (0.26, 0.25, 0.06)
            s.rotation_euler[2] = rot
            bpy.ops.mesh.primitive_cube_add(size=1, location=(x - fx * 0.22, y - fy * 0.22, 0.80))
            b = add(bpy.context.object, m_seat)
            b.scale = (0.26, 0.04, 0.38)
            b.rotation_euler = (math.radians(22), 0, rot)   # reclined away from the facing direction
            bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y, 0.21))
            f = add(bpy.context.object, m_seat_frame)
            f.scale = (0.22, 0.22, 0.21)
            f.rotation_euler[2] = rot
            n += 1
    # step lights along the centre aisle
    for rr in rows:
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0.0, -rr - 0.2, 0.05))
        st = add(bpy.context.object, m_step)
        st.scale = (0.4, 0.02, 0.02)
    # rear console (the operator's desk) and a front marker low on the wall (helps orientation checks; dim)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, R - 1.2, 0.5))
    c = add(bpy.context.object, m_console)
    c.scale = (1.2, 0.5, 0.5)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, -(R - 0.08), 0.5))
    fm = add(bpy.context.object, m_front)
    fm.scale = (0.5, 0.02, 0.05)
    # world: black
    sc.world = bpy.data.worlds.new("World")
    sc.world.use_nodes = True
    sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0, 0, 0, 1)
    # a little fill so the seats read in the dark
    bpy.ops.object.light_add(type='AREA', location=(0, 0, SPRING - 0.3))
    fill = bpy.context.object
    fill.data.energy = 220
    fill.data.size = 6
    fill.data.color = (1.0, 0.85, 0.7)
    fill.rotation_euler = (0, 0, 0)
    # camera: stereo equirect
    bpy.ops.object.camera_add(location=(0, 0, EYE), rotation=(math.radians(90), 0, math.radians(180)))   # image centre = -Y = the front
    cam = bpy.context.object
    cam.name = "SeatCam"
    cam.data.type = 'PANO'
    try:
        cam.data.panorama_type = 'EQUIRECTANGULAR'
    except Exception:
        cam.data.cycles.panorama_type = 'EQUIRECTANGULAR'
    cam.data.stereo.convergence_mode = 'OFFAXIS'
    cam.data.stereo.interocular_distance = 0.064
    cam.data.stereo.use_spherical_stereo = True
    cam.data.stereo.use_pole_merge = True
    cam.data.stereo.pole_merge_angle_from = math.radians(60)
    cam.data.stereo.pole_merge_angle_to = math.radians(75)
    sc.camera = cam
    # render settings
    sc.render.engine = 'CYCLES'
    sc.cycles.samples = 128
    sc.cycles.use_denoising = True
    sc.cycles.device = 'GPU'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type = 'METAL'
        for d in prefs.get_devices_for_type('METAL'):
            d.use = True
    except Exception:
        pass
    sc.render.resolution_x = 2048
    sc.render.resolution_y = 1024
    sc.render.resolution_percentage = 100
    sc.render.use_multiview = True
    sc.render.views_format = 'STEREO_3D'
    sc.render.image_settings.file_format = 'JPEG'
    sc.render.image_settings.quality = 92
    sc.render.image_settings.views_format = 'STEREO_3D'
    sc.render.image_settings.stereo_3d_format.display_mode = 'TOPBOTTOM'
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.exposure = 0.7
    return cam, n


def render(cam, outdir, seats):
    sc = bpy.context.scene
    for i in seats:
        x, y = SEATS[i]
        cam.location = (x, y, EYE)
        sc.render.filepath = os.path.join(outdir, "seat%d.jpg" % i)
        bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    outdir = args[0] if args else os.path.expanduser("~/VRLink/xfer/theatre")
    seats = [int(a) for a in args[1:]] if len(args) > 1 else [0, 1, 2, 3, 4]
    os.makedirs(outdir, exist_ok=True)
    cam, n = build()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(outdir, "theatre.blend"))
    print("theatre: %d seats modelled" % n)
    if bpy.app.background:
        render(cam, outdir, seats)
