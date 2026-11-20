"""Bridge tests against the fake Resolve: run with `python3 -m pytest mac/bridge/tests` or plain `python3 test_bridge.py`.

They cover what the headset relies on: the timeline snapshot, the params message (sections, names, colour triplets,
virtual Scale/card-rotation controls, switch detection, sticky ranges), set/undo/paste round trips, keyframes at the
playhead's comp time, the Fusion-page playhead fallback and the transient-None guard.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# the bridge lives next to the tests in the source tree, two levels up in the release package
for cand in (os.path.dirname(HERE), os.path.dirname(os.path.dirname(HERE))):
    if os.path.exists(os.path.join(cand, "vrlink_bridge.py")):
        sys.path.insert(0, cand)

import fake_resolve  # noqa: E402

sys.modules["DaVinciResolveScript"] = fake_resolve
import vrlink_bridge as vb  # noqa: E402

vb.LOG = os.path.join(HERE, "test-bridge.log")   # never the real bridge.log


class Client:
    def __init__(self):
        self.msgs = []


def make_bridge():
    b = vb.Bridge()
    assert b.connect()
    sent = []
    b.send = lambda client, msg: sent.append(msg)
    b.broadcast = lambda msg: sent.append(msg)
    return b, sent


def params_of(b):
    msg = b.macro_params()
    assert msg["type"] == "params"
    # the FactWindow card on V2 comes first (top track), the plate's PanoMap on V1 second
    assert [g["tool"] for g in msg["groups"]] == ["Skylight_FactWindow", "PanoMap1"]
    return msg["groups"][0]


def test_title_macro_folds_into_the_canvas_control_it_feeds():
    """Nelson, 20 Nov: the headset listed 'title test editorial' and 'footage window' as two cards for one adjustment
    clip, and the title's controls were 'green, blue, slab, key' — the card is one thing with the title's pages."""
    b, _ = make_bridge()
    tl = b.project.GetCurrentTimeline()
    title = fake_resolve.title_macro()
    tl.comp.tools.append(title)
    title.output.connect(tl.macro.inputs["SceneInput"])
    tl.macro.values["Source"] = 2.0
    msg = b.macro_params()
    cards = [g for g in msg["groups"] if g["kind"] == "card"]
    assert [g["tool"] for g in cards] == ["Skylight_FactWindow"]           # one card, not two
    g = cards[0]
    assert g["label"] == "Title \u00b7 Editorial" and g["children"] == ["VelariaTitleTest_Editorial"]
    by_id = {q["id"]: q for q in g["params"]}
    assert "FrameColour" not in by_id and "GlowAmount" not in by_id and "Source" in by_id   # window shows nothing: no border/glow tabs
    face = by_id[">VelariaTitleTest_Editorial|FaceR"]
    assert face["kind"] == "colour" and face["name"] == "Face colour" and face["section"] == "Text"
    assert face["ids"] == [">VelariaTitleTest_Editorial|FaceR", ">VelariaTitleTest_Editorial|FaceG", ">VelariaTitleTest_Editorial|FaceB"]
    assert ">VelariaTitleTest_Editorial|FaceG" not in by_id                  # no loose Green / Blue sliders
    assert by_id[">VelariaTitleTest_Editorial|PlateW"]["name"] == "Width" and by_id[">VelariaTitleTest_Editorial|PlateW"]["section"] == "Plate"
    assert by_id[">VelariaTitleTest_Editorial|PlateR"]["name"] == "Colour"
    key = by_id[">VelariaTitleTest_Editorial|KeyYaw"]
    assert key["name"] == "Key light yaw" and key["section"] == "Lighting"
    env = by_id[">VelariaTitleTest_Editorial|EnvOverUnder"]
    assert env["kind"] == "bool" and env["name"] == "Environment is over/under stereo"
    # writes through the folded id reach the title macro, and undo puts it back
    b.comp_cache  # filled by macro_params
    c = Client()
    b.handle(c, {"cmd": "set", "clip": g["clip"], "tool": g["tool"], "input": ">VelariaTitleTest_Editorial|KeyYaw", "value": -30.0})
    b.apply_pending()
    assert title.values["KeyYaw"] == -30.0
    assert b.undo() == ">VelariaTitleTest_Editorial|KeyYaw" or title.values["KeyYaw"] == 55.0
    assert title.values["KeyYaw"] == 55.0


def test_card_labels_follow_what_the_window_shows():
    b, _ = make_bridge()
    tl = b.project.GetCurrentTimeline()
    g = params_of(b)
    assert g["label"] == "Footage window"
    tl.macro.values["Source"] = 1.0
    assert params_of(b)["label"] == "Graphic"
    tl.macro.values["Source"] = 2.0
    assert params_of(b)["label"] == "3D scene"


def test_timeline_snapshot():
    b, _ = make_bridge()
    t = b.timeline_snapshot()
    names = [c["name"] for c in t["clips"]]
    assert "Cross Dissolve" not in names
    assert names == ["plate.braw", "plate2.braw", "Card"]
    card = [c for c in t["clips"] if c["name"] == "Card"][0]
    assert card["fusion"] and card["color"] == "Pink" and card["track"] == 2
    assert t["markers"][0]["frame"] == 108000 + 120
    assert t["fps"] == 30.0


def test_params_sections_names_and_widgets():
    b, _ = make_bridge()
    g = params_of(b)
    by_id = {p["id"]: p for p in g["params"]}
    # colour triplet folded into one control
    assert by_id["FrameColour"]["kind"] == "colour"
    assert by_id["FrameColour"]["ids"] == ["FrameColour", "FrameColourG", "FrameColourB"]
    assert "FrameColourG" not in by_id
    # 0/1 input is a switch with a professional name
    assert by_id["Mode"]["kind"] == "bool" and by_id["Mode"]["name"] == "Stereo source (L/R)"
    # display names and tab names
    assert by_id["WindowDepth"]["name"] == "Stereo depth" and by_id["WindowDepth"]["section"] == "Stereo"
    assert by_id["FrameThickness"]["section"] == "Border"
    assert by_id["GlowAmount"]["name"] == "Glow opacity"
    assert by_id["Yaw"]["name"] == "Pan" and by_id["Yaw"].get("invert") is True
    # virtual controls reached through the macro's children
    assert by_id["@size"]["name"] == "Scale" and by_id["@size"]["section"] == "Placement"
    assert by_id["@turn"]["name"] == "Yaw (card)" and by_id["@turn"]["section"] == "Object"
    # order: Placement, Object, Stereo, ..., Output
    sections = []
    for p in g["params"]:
        if p["section"] not in sections:
            sections.append(p["section"])
    assert sections[:3] == ["Placement", "Object", "Stereo"]
    # source line
    assert g["source"] == "Shot L/R.mov"
    # Comments (text) is hidden
    assert "Comments" not in by_id


def test_out_of_range_values_widen_and_stick():
    b, _ = make_bridge()
    g = params_of(b)
    dist = [p for p in g["params"] if p["id"] == "Distance"][0]
    assert dist["value"] == -0.2 and dist["max"] >= -0.2   # macro scale ends at -0.5; value is beyond it
    # back inside the macro's range the slider keeps the room it had
    b.timeline.macro.values["Distance"] = -1.0
    g2 = params_of(b)
    dist2 = [p for p in g2["params"] if p["id"] == "Distance"][0]
    assert dist2["max"] == dist["max"]


def test_set_undo_round_trip():
    b, sent = make_bridge()
    g = params_of(b)   # fills comp_cache
    macro = b.timeline.macro
    b.handle(None, {"cmd": "set", "clip": g["clip"], "tool": g["tool"], "input": "GlowAmount", "value": 0.25})
    b.apply_pending()
    assert macro.values["GlowAmount"] == 0.25
    # virtual control writes to both planes
    b.handle(None, {"cmd": "set", "clip": g["clip"], "tool": g["tool"], "input": "@turn", "value": 33.0})
    b.apply_pending()
    left, right = macro.children[0], macro.children[1]
    assert left.values["Transform3DOp.Rotate.Y"] == 33.0 and right.values["Transform3DOp.Rotate.Y"] == 33.0
    # scale drives X only while the lock is on
    b.handle(None, {"cmd": "set", "clip": g["clip"], "tool": g["tool"], "input": "@size", "value": 2.0})
    b.apply_pending()
    assert left.values["Transform3DOp.Scale.X"] == 2.0
    # undo walks back in order
    b.handle(None, {"cmd": "undo"})
    assert left.values["Transform3DOp.Scale.X"] == 1.0
    b.handle(None, {"cmd": "undo"})
    assert left.values["Transform3DOp.Rotate.Y"] == 0.0
    b.handle(None, {"cmd": "undo"})
    assert macro.values["GlowAmount"] == 0.7
    notices = [m for m in sent if m.get("type") == "notice"]
    assert notices and notices[-1]["msg"].startswith("Restored")


def test_paste_is_one_undo_step():
    b, sent = make_bridge()
    g = params_of(b)
    macro = b.timeline.macro
    b.handle(None, {"cmd": "paste", "clip": g["clip"], "tool": g["tool"],
                    "values": {"GlowAmount": 0.1, "CornerRadius": 0.3, "Yaw": -40.0}})
    assert macro.values["GlowAmount"] == 0.1 and macro.values["Yaw"] == -40.0
    assert b.timeline.comp.locks == 0
    b.handle(None, {"cmd": "undo"})
    assert macro.values["GlowAmount"] == 0.7 and macro.values["CornerRadius"] == 0.06 and macro.values["Yaw"] == 20.0
    assert any(m.get("type") == "notice" and m["msg"] == "Undid paste" for m in sent)


def test_keyframe_lands_at_comp_time():
    b, sent = make_bridge()
    g = params_of(b)
    macro = b.timeline.macro
    b.handle(None, {"cmd": "key", "clip": g["clip"], "tool": g["tool"], "inputs": ["GlowAmount"], "values": [0.5]})
    assert "GlowAmount" in macro.modifiers
    # playhead 108350, clip starts 108300, global start 100 -> comp time 150
    assert macro.inputs["GlowAmount"].keys == {150.0: 0.5}
    # later sets on an animated input also go to the playhead time
    b.timeline.frame = 108360
    b.handle(None, {"cmd": "set", "clip": g["clip"], "tool": g["tool"], "input": "GlowAmount", "value": 0.9})
    b.apply_pending()
    assert macro.inputs["GlowAmount"].keys[160.0] == 0.9
    assert any(m.get("type") == "notice" and m["msg"].startswith("Keyframe set") for m in sent)


def test_fusion_page_playhead_and_transient_none():
    b, _ = make_bridge()
    b.timeline.page_fusion = True
    b.timeline.comp.current_time = 100 + 42
    assert b.current_frame() == 108300 + 42
    # a clip answering None for its start is a transient, not a lost Resolve
    orig = b.timeline.tracks[1][0].GetStart
    b.timeline.tracks[1][0].GetStart = lambda: None
    try:
        try:
            b.timeline_snapshot()
            assert False, "expected TransientError"
        except vb.TransientError:
            pass
    finally:
        b.timeline.tracks[1][0].GetStart = orig


def test_panomap_is_exposed_as_orientation():
    b, sent = make_bridge()
    msg = b.macro_params()
    pano = msg["groups"][1]
    ids = [p["id"] for p in pano["params"]]
    assert ids == ["Rotate.X", "Rotate.Y", "Rotate.Z"]          # whitelist: no RotOrder, no Comments
    by_id = {p["id"]: p for p in pano["params"]}
    assert by_id["Rotate.X"]["name"] == "Tilt" and by_id["Rotate.Y"]["name"] == "Pan" and by_id["Rotate.Z"]["name"] == "Roll"
    assert all(p["section"] == "Placement" for p in pano["params"])
    assert by_id["Rotate.Z"]["value"] == -3.0
    # it takes sets and undo like a macro
    b.handle(None, {"cmd": "set", "clip": pano["clip"], "tool": "PanoMap1", "input": "Rotate.Z", "value": 0.0})
    assert b.timeline.pano.values["Rotate.Z"] == 0.0       # applied at once, not on the worker tick
    b.handle(None, {"cmd": "undo"})
    assert b.timeline.pano.values["Rotate.Z"] == -3.0


def test_pan_and_tilt_batch_locks_the_comp_once():
    b, _ = make_bridge()
    g = params_of(b)
    comp = b.timeline.comp
    locks = []
    orig_lock, orig_unlock = comp.Lock, comp.Unlock
    comp.Lock = lambda: (locks.append("L"), orig_lock())
    comp.Unlock = lambda: (locks.append("U"), orig_unlock())
    with b.lock:
        b.pending[(g["clip"], g["tool"], "Yaw")] = 5.0
        b.pending[(g["clip"], g["tool"], "Pitch")] = -7.0
        b.apply_pending()
    assert locks == ["L", "U"] and comp.locks == 0
    assert b.timeline.macro.values["Yaw"] == 5.0 and b.timeline.macro.values["Pitch"] == -7.0


def test_clip_index_follows_the_timeline():
    b, _ = make_bridge()
    assert [it.GetName() for _, it in b.items_under_playhead()] == ["Card", "plate.braw"]
    b.timeline.frame = 108700
    assert [it.GetName() for _, it in b.items_under_playhead()] == []
    # a clip that gains a comp shows up after the next rebuild
    b.timeline.tracks[1][1].comp = b.timeline.plate_comp
    assert b.items_under_playhead() == []
    b.rebuild_index()
    assert [it.GetName() for _, it in b.items_under_playhead()] == ["plate2.braw"]


def test_seek_and_step():
    b, _ = make_bridge()
    b.handle(None, {"cmd": "seek", "frame": 108400})
    assert b.timeline.frame == 108400
    b.handle(None, {"cmd": "step", "delta": -1})
    assert b.timeline.frame == 108399
    b.handle(None, {"cmd": "clip", "dir": 1})
    assert b.timeline.frame in (108500, 108600)


def test_group_kinds_and_nested_macro_children():
    # layers: a MacroOperator is a card, PanoMap / Velaria_Orient* are orient, LatLongPatcher is a patch; tools
    # inside a macro (Orient 360's two PanoMaps) never become groups of their own
    b, _ = make_bridge()
    inner = [fake_resolve.pano_map("PanoMap_L"), fake_resolve.pano_map("PanoMap_R")]
    orient = fake_resolve.Tool("Velaria_Orient360", "MacroOperator", [
        ("Pan", "Pan", "Number", "ScrewControl", -180.0, 180.0, 4.0)], children=inner)
    patch = fake_resolve.Tool("LatLongPatcher1", "LatLongPatcher", [
        ("Rotate.X", "Rotate X", "Number", "ScrewControl", -180.0, 180.0, 90.0)])
    plate = b.timeline.plate_comp
    plate.tools.extend([orient] + inner + [patch])
    msg = b.macro_params()
    kinds = {g["tool"]: g.get("kind") for g in msg["groups"]}
    assert kinds["Skylight_FactWindow"] == "card"
    assert kinds["PanoMap1"] == "orient"
    assert kinds["Velaria_Orient360"] == "orient"
    assert kinds["LatLongPatcher1"] == "patch"
    assert "PanoMap_L" not in kinds and "PanoMap_R" not in kinds
    # cards first, then the rest
    order = [g.get("kind") for g in msg["groups"]]
    assert order[0] == "card" and "card" not in order[1:]


def test_trim_macro_is_kind_trim_with_named_controls_in_a_trim_tab():
    # Velaria Trim (ColorCorrector on an adjustment clip) grades the whole picture: kind "trim", after cards and orient,
    # before patches; its controls keep their names and land in one Trim section
    b, _ = make_bridge()
    trim = fake_resolve.Tool("Velaria_Trim", "MacroOperator", [
        ("Exposure", "Exposure (offset)", "Number", "SliderControl", -0.5, 0.5, 0.0),
        ("Gamma", "Gamma", "Number", "SliderControl", 0.5, 2.0, 1.0),
        ("Temperature", "Temperature (blue - amber)", "Number", "SliderControl", -1.0, 1.0, 0.0),
        ("Amount", "Amount (0 = off)", "Number", "SliderControl", 0.0, 1.0, 1.0)])
    patch = fake_resolve.Tool("LatLongPatcher1", "LatLongPatcher", [
        ("Rotate.X", "Rotate X", "Number", "ScrewControl", -180.0, 180.0, 90.0)])
    b.timeline.plate_comp.tools.extend([trim, patch])
    msg = b.macro_params()
    g = {x["tool"]: x for x in msg["groups"]}
    assert g["Velaria_Trim"]["kind"] == "trim"
    order = [x["kind"] for x in msg["groups"]]
    assert order.index("trim") > order.index("card") and order.index("trim") < order.index("patch")
    names = {p["id"]: p["name"] for p in g["Velaria_Trim"]["params"]}
    assert names["Exposure"] == "Exposure" and names["Temperature"] == "Temperature" and names["Amount"] == "Amount"
    assert {p.get("section") for p in g["Velaria_Trim"]["params"]} == {"Trim"}


def test_seek_to_frame_from_the_strip():
    # the empty strip's "previous / next overlay" chips send a plain seek to an absolute frame
    b, _ = make_bridge()
    b.handle(None, {"cmd": "seek", "frame": 108600})
    assert b.timeline.frame == 108600


def test_hello_leads_with_versions():
    # the first reply to hello names the bridge, the installed package, Resolve and the project
    b, sent = make_bridge()
    b.handle(None, {"cmd": "hello"})
    assert sent[0]["type"] == "hello"
    assert sent[0]["bridge"] == vb.BRIDGE_VERSION
    assert [m["type"] for m in sent[1:4]] == ["timeline", "state", "params"]


class _Settings:
    """A timeline/project whose GetSetting answers from a dict (the shapes Resolve 21 returns: strings)."""
    def __init__(self, d): self.d = d
    def GetSetting(self, k=None): return self.d if k is None else self.d.get(k, "0")


def test_preflight_square_360_needs_output_scaling():
    b, _ = make_bridge()
    b.fps = 29.97
    square = {"useCustomSettings": "1", "timelineResolutionWidth": "5760", "timelineResolutionHeight": "5760",
              "timelineOutputResMatchTimelineRes": "1"}
    fmt = b.timeline_format(_Settings(square))
    assert fmt == {"width": 5760, "height": 5760, "outWidth": 5760, "outHeight": 5760, "fps": 29.97, "custom": True}
    w = vb.preflight(fmt)
    assert len(w) == 1 and "Output scaling > 3840 x 3840" in w[0] and "5760x5760" in w[0]
    # the fix applied: output scaling 3840 x 3840 -> quiet
    scaled = dict(square, timelineOutputResMatchTimelineRes="0", timelineOutputResolutionWidth="3840", timelineOutputResolutionHeight="3840")
    assert vb.preflight(b.timeline_format(_Settings(scaled))) == []


def test_preflight_quiet_for_vr180_and_8k_uhd_and_wide_fit():
    b, _ = make_bridge()
    for w, h in ((5760, 2880), (7680, 4320), (3840, 1920), (1920, 1080)):
        fmt = b.timeline_format(_Settings({"useCustomSettings": "1", "timelineResolutionWidth": str(w), "timelineResolutionHeight": str(h)}))
        assert vb.preflight(fmt) == [], (w, h)
    # wider than the raster: suggest a fit that keeps the aspect, not the square value
    fmt = b.timeline_format(_Settings({"useCustomSettings": "1", "timelineResolutionWidth": "8192", "timelineResolutionHeight": "4096"}))
    assert vb.preflight(fmt) == ["Timeline 8192x4096 exceeds the 7680x4320 monitor: Timeline Settings > Output scaling > 7680 x 3840"]


def test_timeline_format_falls_back_to_project_settings():
    # useCustomSettings off: the timeline inherits the project's size (Strange Worlds reads 5000x2000 at project level)
    b, _ = make_bridge()
    b.project = _Settings({"timelineResolutionWidth": "5000", "timelineResolutionHeight": "2000", "timelineOutputResMatchTimelineRes": "1"})
    fmt = b.timeline_format(_Settings({"useCustomSettings": "0"}))
    assert (fmt["width"], fmt["height"], fmt["outWidth"], fmt["outHeight"], fmt["custom"]) == (5000, 2000, 5000, 2000, False)


def test_timeline_message_carries_format_and_warnings():
    b, sent = make_bridge()
    msg = b.timeline_snapshot()
    assert "format" in msg and "warnings" in msg and isinstance(msg["warnings"], list)


def test_macro_kind_accepts_both_velaria_and_legacy_kalyx_prefixes():
    # timelines built before the rename keep their Kalyx_* instances until the upgrade script runs; the headset
    # must still put them on the right layer
    mk = vb.Bridge.macro_kind
    assert mk("Velaria_Trim") == "trim" and mk("Kalyx_Trim") == "trim" and mk("Kalyx_Trim_2") == "trim"
    assert mk("Velaria_Orient360") == "orient" and mk("Kalyx_Orient360") == "orient"
    assert mk("Velaria_CanvasControl") == "card" and mk("Kalyx_CanvasControl") == "card" and mk("Skylight_FactWindow") == "card"
    assert mk("Velaria_Stereo180") == "stereo" and mk("Velaria_Stereo360_2") == "stereo" and mk("Velaria_Orient180") == "orient"


# ---- the wire path: what a headset (or anything else that connects) can send ----

def wire_bridge():
    """A bridge with one connected client on a socket pair, its client loop running as it does in production."""
    import socket
    import threading
    b = vb.Bridge()
    assert b.connect()
    b.macro_params()   # what the periodic loop does once a headset is connected: fills the comp cache
    ours, theirs = socket.socketpair()
    b.clients.append(theirs)
    t = threading.Thread(target=b.client_loop, args=(theirs, ("test", 0)), daemon=True)
    t.start()
    return b, ours, t


def wait_for(cond, seconds=2.0):
    import time
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.005)
    return False


def test_sets_sent_back_to_back_cost_one_comp_lock():
    # A grab moves pan and tilt, a colour moves three channels: the headset sends one "set" line per input, back to
    # back. They must reach Resolve as one locked batch (one render), whether they share a read or not.
    import json
    b, ours, _ = wire_bridge()
    comp, macro = b.timeline.comp, b.timeline.macro
    locks = []
    orig_lock = comp.Lock
    comp.Lock = lambda: (locks.append("L"), orig_lock())
    line = lambda pid, v: (json.dumps({"cmd": "set", "clip": "V2:108300", "tool": "Skylight_FactWindow",
                                       "input": pid, "value": v}) + "\n").encode()
    ours.sendall(line("Yaw", 5.0) + line("Pitch", -7.0))          # one segment
    assert wait_for(lambda: macro.values["Pitch"] == -7.0)
    assert macro.values["Yaw"] == 5.0 and locks == ["L"] and comp.locks == 0
    ours.sendall(line("FrameColour", 0.1))                          # three segments, as TCP_NODELAY sends them
    ours.sendall(line("FrameColourG", 0.2))
    ours.sendall(line("FrameColourB", 0.3))
    assert wait_for(lambda: macro.values["FrameColourB"] == 0.3)
    assert (macro.values["FrameColour"], macro.values["FrameColourG"]) == (0.1, 0.2)
    assert locks == ["L", "L"] and comp.locks == 0
    ours.sendall(line("GlowAmount", 0.4))                           # a single input needs no lock
    assert wait_for(lambda: macro.values["GlowAmount"] == 0.4)
    assert locks == ["L", "L"]
    ours.close()


def test_garbage_on_the_wire_is_survived():
    import json
    b, ours, t = wire_bridge()
    macro = b.timeline.macro
    ours.sendall(b"not json\n[1,2,3]\n42\n\"text\"\n{\"cmd\":\"set\"}\n{\"cmd\":\"seek\"}\n")
    ours.sendall((json.dumps({"cmd": "set", "clip": "V2:108300", "tool": "Skylight_FactWindow", "input": "Roll",
                              "value": 9.0}) + "\n").encode())
    assert wait_for(lambda: macro.values["Roll"] == 9.0)   # still serving after the nonsense
    assert t.is_alive()
    ours.close()


def test_a_line_that_never_ends_drops_the_client():
    b, ours, t = wire_bridge()
    old = vb.MAX_LINE
    vb.MAX_LINE = 4096
    try:
        try:
            ours.sendall(b"x" * 20000)
        except OSError:
            pass
        assert wait_for(lambda: not t.is_alive())
        assert b.clients == []
    finally:
        vb.MAX_LINE = old
        ours.close()


def test_deeply_nested_line_does_not_kill_the_thread_or_leak_its_slot():
    # json.loads raises RecursionError (not ValueError) on a line of "[" characters. It used to end the client's
    # thread with the client still listed, so eight such lines filled every slot until the bridge was restarted.
    import json
    b, ours, t = wire_bridge()
    macro = b.timeline.macro
    ours.sendall(b"[" * 200000 + b"\n")
    ours.sendall((json.dumps({"cmd": "set", "clip": "V2:108300", "tool": "Skylight_FactWindow", "input": "Roll",
                              "value": 4.0}) + "\n").encode())
    assert wait_for(lambda: macro.values["Roll"] == 4.0)   # still serving this client
    assert t.is_alive()
    ours.close()
    assert wait_for(lambda: not t.is_alive())
    assert b.clients == []                                 # and the slot comes back when it leaves


def test_a_client_thread_that_dies_gives_its_slot_back():
    b, ours, t = wire_bridge()
    def boom(client, buf):
        raise RuntimeError("anything unexpected")
    b.read_commands = boom
    ours.sendall(b"{}\n")
    t.join(2.0)
    assert not t.is_alive() and b.clients == []
    ours.close()


def test_inner_tools_are_listed_once_per_drag_not_once_per_step():
    b, _ = make_bridge()
    g = params_of(b)
    macro = b.timeline.macro
    calls = []
    orig = macro.GetChildrenList
    macro.GetChildrenList = lambda: (calls.append(1), orig())[1]
    b.macro_params()
    base = len(calls)
    for v in (10.0, 11.0, 12.0, 13.0):
        b.handle(None, {"cmd": "set", "clip": g["clip"], "tool": g["tool"], "input": "@turn", "value": v})
    assert len(calls) == base and b.timeline.macro.children[0].values["Transform3DOp.Rotate.Y"] == 13.0
    b.macro_params()                      # fresh tool objects: the list is read again
    assert len(calls) > base


def test_listener_waits_for_a_busy_port_and_caps_connections():
    import socket
    import threading
    import time
    blocker = socket.socket()
    blocker.bind(("127.0.0.1", 0))
    blocker.listen(1)
    port = blocker.getsockname()[1]
    old_port, old_env, old_sleep = vb.PORT, os.environ.get("VRLINK_BRIDGE_BIND"), vb.time.sleep
    vb.PORT, os.environ["VRLINK_BRIDGE_BIND"] = port, "127.0.0.1"
    vb.time.sleep = lambda s: old_sleep(min(s, 0.05))
    try:
        b = vb.Bridge()
        assert b.connect()
        t = threading.Thread(target=b.accept_loop, daemon=True)
        t.start()
        time.sleep(0.2)
        assert t.is_alive()                 # the port is taken: still trying, not dead
        blocker.close()
        conns = []
        deadline = time.time() + 3
        while time.time() < deadline and len(conns) < vb.MAX_CLIENTS + 4:
            try:
                conns.append(socket.create_connection(("127.0.0.1", port), timeout=0.5))
            except OSError:
                time.sleep(0.05)
        assert len(conns) == vb.MAX_CLIENTS + 4
        assert wait_for(lambda: len(b.clients) == vb.MAX_CLIENTS)
        time.sleep(0.2)
        assert len(b.clients) == vb.MAX_CLIENTS
        for c in conns:
            c.close()
        assert wait_for(lambda: len(b.clients) == 0)
    finally:
        vb.time.sleep = old_sleep
        vb.PORT = old_port
        os.environ.pop("VRLINK_BRIDGE_BIND", None)
        if old_env is not None:
            os.environ["VRLINK_BRIDGE_BIND"] = old_env


def test_log_rotates_and_set_lines_stay_out_of_stderr():
    import io
    import tempfile
    d = tempfile.mkdtemp()
    old_log, old_max, old_err = vb.LOG, vb.LOG_MAX_BYTES, sys.stderr
    vb.LOG, vb.LOG_MAX_BYTES = os.path.join(d, "bridge.log"), 200
    sys.stderr = io.StringIO()
    try:
        with open(vb.LOG, "w") as f:
            f.write("x" * 500)
        vb.log("first")
        vb.log("quiet", echo=False)
        assert os.path.getsize(os.path.join(d, "bridge.old.log")) == 500
        text = open(vb.LOG).read()
        assert "first" in text and "quiet" in text and "xxx" not in text
        assert "first" in sys.stderr.getvalue() and "quiet" not in sys.stderr.getvalue()
    finally:
        sys.stderr = old_err
        vb.LOG, vb.LOG_MAX_BYTES = old_log, old_max
        vb.log("back")   # reopen on the test log


def test_network_usb_binds_loopback_only():
    import plistlib
    import tempfile
    d = tempfile.mkdtemp()
    old_cfg, old_env = vb.CONFIG, os.environ.pop("VRLINK_BRIDGE_BIND", None)
    try:
        vb.CONFIG = os.path.join(d, "missing.plist")
        assert vb.bind_address() == "0.0.0.0"                       # no settings file: unchanged behaviour
        vb.CONFIG = os.path.join(d, "config.plist")
        with open(vb.CONFIG, "wb") as f:
            plistlib.dump({"Layout": "auto"}, f)
        assert vb.bind_address() == "0.0.0.0"                       # key absent: unchanged behaviour
        with open(vb.CONFIG, "wb") as f:
            plistlib.dump({"Network": "USB"}, f)
        assert vb.bind_address() == "127.0.0.1"
        os.environ["VRLINK_BRIDGE_BIND"] = "127.0.0.1"
        vb.CONFIG = os.path.join(d, "missing.plist")
        assert vb.bind_address() == "127.0.0.1"
    finally:
        vb.CONFIG = old_cfg
        os.environ.pop("VRLINK_BRIDGE_BIND", None)
        if old_env is not None:
            os.environ["VRLINK_BRIDGE_BIND"] = old_env


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("ok   ", name)
            except Exception as e:   # noqa: BLE001
                failed += 1
                import traceback
                print("FAIL ", name, "-", e)
                traceback.print_exc()
    print("FAILED" if failed else "PASSED", failed, "failure(s)")
    sys.exit(1 if failed else 0)


def test_hide_and_show_clips_from_the_headset():
    b, sent = make_bridge()
    card = [c for c in b.timeline_snapshot()["clips"] if c["name"] == "Card"][0]
    assert card["enabled"] is True
    sig_before = b.signature()
    b.handle(Client(), {"cmd": "hide", "clips": [card["key"]], "on": 1})
    snap = [m for m in sent if m.get("type") == "timeline"][-1]
    assert [c["enabled"] for c in snap["clips"] if c["name"] == "Card"] == [False]
    assert [c["enabled"] for c in snap["clips"] if c["name"] != "Card"] == [True, True]
    assert sent[-1]["type"] == "notice" and sent[-1]["msg"] == "Hidden 1 clip"
    assert b.signature() != sig_before          # a toggle made in Resolve itself would reach the headset the same way
    b.handle(Client(), {"cmd": "hide", "clips": [card["key"]], "on": 0})
    snap = [m for m in sent if m.get("type") == "timeline"][-1]
    assert all(c["enabled"] for c in snap["clips"])
    assert sent[-1]["msg"] == "Showing 1 clip"
    # hiding something already hidden, or an unknown key, is quiet
    n = len(sent)
    b.handle(Client(), {"cmd": "hide", "clips": ["V9:1"], "on": 1})
    assert [m["type"] for m in sent[n:]] == ["timeline"]


def test_markers_from_the_headset():
    b, sent = make_bridge()
    tl = b.timeline
    # playhead at 108350: a note with the gaze lands at relative frame 350, Cyan by default
    b.handle(Client(), {"cmd": "marker", "action": "add", "pan": 35.2, "tilt": -10.4, "card": "Title card"})
    mk = tl.markers[350]
    assert mk["color"] == "Cyan" and mk["name"] == "Headset note"
    assert "yaw +35\u00b0, pitch -10\u00b0" in mk["note"] and "Picked: Title card" in mk["note"]
    assert mk["customData"] == "velaria:pan=35.2;tilt=-10.4"
    assert sent[-1] == {"type": "notice", "msg": "Cyan marker at 01:00:11:20"}
    snap = [m for m in sent if m.get("type") == "timeline"][-1]
    assert [m["frame"] for m in snap["markers"]] == [108120, 108350]
    # a second note on the same frame replaces the first (Resolve keeps one per frame)
    b.handle(Client(), {"cmd": "marker", "action": "add", "color": "Red"})
    assert tl.markers[350]["color"] == "Red" and len(tl.markers) == 2
    # jump: previous marker, then next
    b.handle(Client(), {"cmd": "marker", "action": "jump", "dir": -1})
    assert tl.frame == 108120
    b.handle(Client(), {"cmd": "marker", "action": "jump", "dir": 1})
    assert tl.frame == 108350
    # remove under the playhead
    b.handle(Client(), {"cmd": "marker", "action": "remove"})
    assert 350 not in tl.markers and sent[-1]["msg"] == "Marker removed at 01:00:11:20"
    b.handle(Client(), {"cmd": "marker", "action": "remove"})
    assert sent[-1]["msg"] == "No marker under the playhead"
    # an unknown colour falls back to Cyan
    b.handle(Client(), {"cmd": "marker", "action": "add", "color": "Octarine"})
    assert tl.markers[350]["color"] == "Cyan"


def test_still_from_the_headset(tmp_path):
    b, sent = make_bridge()
    # no clip path known: falls back to ~/Pictures/Velaria Stills (redirected for the test)
    import os
    b.stills_dir = lambda: str(tmp_path)
    b.handle(Client(), {"cmd": "still"})
    files = sorted(os.listdir(tmp_path))
    assert files == ["Test timeline 01-00-11-20.jpg"]
    assert sent[-1]["type"] == "notice" and sent[-1]["msg"].startswith("Still saved: ")
    # a second grab on the same frame does not overwrite
    b.handle(Client(), {"cmd": "still"})
    assert sorted(os.listdir(tmp_path)) == ["Test timeline 01-00-11-20 (2).jpg", "Test timeline 01-00-11-20.jpg"]


def test_stills_land_beside_the_clip_media(tmp_path):
    b, sent = make_bridge()
    class MPI:
        def GetClipProperty(self, k): return str(tmp_path / "footage" / "shot.mov")
    (tmp_path / "footage").mkdir()
    for v in (1, 2):
        for it in b.timeline.tracks[v]: it.mpi = MPI()
    assert b.stills_dir() == str(tmp_path / "footage" / "Velaria Stills")


# ---------------------------------------------------------------- output scaling for the headset (19 Nov)
class _Tl:
    """A timeline (or project) with settable settings, the way the bridge drives Resolve's."""
    def __init__(self, name, d, uid=None):
        self.name, self.d, self.uid, self.writes = name, dict(d), uid or name, []
    def GetName(self): return self.name
    def GetUniqueId(self): return self.uid
    def GetSetting(self, k=None): return self.d if k is None else self.d.get(k, "")
    def SetSetting(self, k, v):
        self.d[k] = v
        self.writes.append((k, v))
        return True


SQUARE = {"useCustomSettings": "1", "timelineResolutionWidth": "5760", "timelineResolutionHeight": "5760",
          "timelineOutputResMatchTimelineRes": "1", "timelineOutputResolutionWidth": "5760", "timelineOutputResolutionHeight": "5760"}


def _gone(b):
    """The headset disconnects and stays away past the 5 s restore grace."""
    b.clients = []
    b.autoscale_tick(now=1000.0)
    b.autoscale_tick(now=1000.0 + vb.RESTORE_GRACE_S + 0.1)


def _scaling_bridge(tmp_path, tl):
    b, sent = make_bridge()
    vb.SIDECAR = str(tmp_path / "output-scaling.json")
    b.timeline, b.fps = tl, 30.0
    b.project = _project(tl, {"timelineResolutionWidth": "1920", "timelineResolutionHeight": "1080"})
    b.refresh_timeline()
    return b, sent


def _project(tl, d):
    p = _Tl("Proj", d)
    p.GetCurrentTimeline = lambda: tl
    p.GetTimelineCount = lambda: 1
    p.GetTimelineByIndex = lambda i: tl
    return p


def test_autoscale_sets_square_on_connect_and_restores_on_disconnect(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    assert "Output scaling > 3840 x 3840" in b.warnings[0]
    b.autoscale_tick()                       # no headset: nothing happens
    assert tl.writes == [] and not os.path.exists(vb.SIDECAR)
    b.clients = [object()]
    b.autoscale_tick()
    assert tl.d["timelineOutputResMatchTimelineRes"] == "0"
    assert (tl.d["timelineOutputResolutionWidth"], tl.d["timelineOutputResolutionHeight"]) == ("3840", "3840")
    assert b.format["outWidth"] == 3840 and "set to 3840 x 3840 for the headset" in b.warnings[0]
    assert os.path.exists(vb.SIDECAR)
    b.autoscale_tick()                       # idempotent while connected
    assert len(tl.writes) == 3
    _gone(b)
    assert tl.d["timelineOutputResMatchTimelineRes"] == "1"
    assert (tl.d["timelineOutputResolutionWidth"], tl.d["timelineOutputResolutionHeight"]) == ("5760", "5760")
    assert b.scaled == {} and not os.path.exists(vb.SIDECAR)
    assert "Output scaling > 3840 x 3840" in b.warnings[0]     # the plain advice is back


def test_autoscale_leaves_fitting_timelines_alone(tmp_path):
    tl = _Tl("01 180 SBS", {"useCustomSettings": "1", "timelineResolutionWidth": "7680", "timelineResolutionHeight": "3840",
                            "timelineOutputResMatchTimelineRes": "1"})
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick()
    assert tl.writes == [] and b.scaled == {}


def test_autoscale_wide_fit_and_project_level_settings(tmp_path):
    # the timeline inherits the project's 8192x4096: the project's settings are what get scaled, aspect kept
    tl = _Tl("wide", {"useCustomSettings": "0"})
    b, _ = _scaling_bridge(tmp_path, tl)
    b.project = _project(tl, {"timelineResolutionWidth": "8192", "timelineResolutionHeight": "4096", "timelineOutputResMatchTimelineRes": "1"})
    b.refresh_timeline()
    b.clients = [object()]
    b.autoscale_tick()
    assert tl.writes == []
    assert (b.project.d["timelineOutputResolutionWidth"], b.project.d["timelineOutputResolutionHeight"]) == ("7680", "3840")
    _gone(b)
    assert b.project.d["timelineOutputResMatchTimelineRes"] == "1"


def test_autoscale_restores_from_sidecar_after_a_crash(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick()
    assert tl.d["timelineOutputResolutionWidth"] == "3840"
    # a new bridge (crash, restart) on the same project finds the sidecar and puts the timeline back first thing
    b2, _ = _scaling_bridge(tmp_path, tl)
    assert not b2.scaled_loaded
    b2.autoscale_tick()
    assert tl.d["timelineOutputResMatchTimelineRes"] == "1" and tl.d["timelineOutputResolutionWidth"] == "5760"
    assert not os.path.exists(vb.SIDECAR)


def test_autoscale_respects_a_hand_change(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick()
    tl.d["timelineOutputResolutionWidth"] = tl.d["timelineOutputResolutionHeight"] = "4320"   # the user's own choice
    _gone(b)
    assert tl.d["timelineOutputResolutionWidth"] == "4320" and tl.d["timelineOutputResMatchTimelineRes"] == "0"
    assert b.scaled == {}


def test_autoscale_reapplies_when_resolve_reverts_the_scaling(tmp_path):
    # 20 Nov: saving the Timeline Settings dialog re-asserted "match timeline resolution" on a scaled timeline;
    # the record said scaled, the headset got the full 5760 square
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick()
    assert tl.d["timelineOutputResolutionWidth"] == "3840"
    tl.d["timelineOutputResMatchTimelineRes"] = "1"
    tl.d["timelineOutputResolutionWidth"] = tl.d["timelineOutputResolutionHeight"] = "5760"
    b.autoscale_tick()
    assert (tl.d["timelineOutputResolutionWidth"], tl.d["timelineOutputResMatchTimelineRes"]) == ("3840", "0")
    assert list(b.scaled)                                   # still managed
    _gone(b)
    assert tl.d["timelineOutputResolutionWidth"] == "5760" and b.scaled == {}


def test_autoscale_hand_change_while_connected_drops_the_record(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick()
    tl.d["timelineOutputResolutionWidth"] = tl.d["timelineOutputResolutionHeight"] = "4320"   # the user's own choice
    b.autoscale_tick()
    assert tl.d["timelineOutputResolutionWidth"] == "4320" and b.scaled == {} and not os.path.exists(vb.SIDECAR)


def test_autoscale_restore_waits_out_a_short_disconnect(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick(now=100.0)
    b.clients = []
    b.autoscale_tick(now=101.0)
    b.autoscale_tick(now=103.0)
    assert tl.d["timelineOutputResolutionWidth"] == "3840"          # still scaled: it may be a reconnect
    b.clients = [object()]
    b.autoscale_tick(now=104.0)
    assert tl.d["timelineOutputResolutionWidth"] == "3840" and len(tl.writes) == 3   # no flap
    _gone(b)
    assert tl.d["timelineOutputResolutionWidth"] == "5760"


def test_autoscale_partial_apply_rolls_back(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    real = tl.SetSetting
    def flaky(k, v):
        if k == "timelineOutputResolutionHeight" and v == "3840":
            return False
        return real(k, v)
    tl.SetSetting = flaky
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick()
    assert (tl.d["timelineOutputResMatchTimelineRes"], tl.d["timelineOutputResolutionWidth"], tl.d["timelineOutputResolutionHeight"]) == ("1", "5760", "5760")
    assert b.scaled == {} and not os.path.exists(vb.SIDECAR)


def test_autoscale_refused_restore_keeps_the_record(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick()
    real = tl.SetSetting
    tl.SetSetting = lambda k, v: False
    _gone(b)
    assert list(b.scaled) and os.path.exists(vb.SIDECAR)          # not forgotten
    tl.SetSetting = real
    b.autoscale_tick(now=2000.0)
    assert tl.d["timelineOutputResolutionWidth"] == "5760" and b.scaled == {}


def test_autoscale_match_checkbox_counts_as_a_hand_change_on_restore(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    b.clients = [object()]
    b.autoscale_tick()
    tl.d["timelineOutputResMatchTimelineRes"] = "1"      # the user ticked it back on, size left at 3840
    writes = len(tl.writes)
    _gone(b)
    assert len(tl.writes) == writes and b.scaled == {}


def test_autoscale_deliver_page_restores_and_reapplies(tmp_path):
    tl = _Tl("13 Chart 360 TB", SQUARE)
    b, _ = _scaling_bridge(tmp_path, tl)
    page = {"p": "edit"}
    class R:
        def GetCurrentPage(self): return page["p"]
    b.resolve = R()
    b.project.IsRenderingInProgress = lambda: False
    b.clients = [object()]
    b.autoscale_tick()
    assert tl.d["timelineOutputResolutionWidth"] == "3840"
    page["p"] = "deliver"
    b.autoscale_tick()
    assert tl.d["timelineOutputResolutionWidth"] == "5760" and b.scaled == {}
    b.autoscale_tick()                                   # stays put while delivering
    assert tl.d["timelineOutputResolutionWidth"] == "5760"
    page["p"] = "edit"
    b.autoscale_tick()
    assert tl.d["timelineOutputResolutionWidth"] == "3840"


def test_autoscale_switching_timelines_restores_the_old_one(tmp_path):
    a = _Tl("A square", SQUARE, uid="a")
    c = _Tl("B square", SQUARE, uid="b")
    b, _ = _scaling_bridge(tmp_path, a)
    b.project.GetTimelineCount = lambda: 2
    b.project.GetTimelineByIndex = lambda i: [a, c][i - 1]
    b.clients = [object()]
    b.autoscale_tick()
    assert a.d["timelineOutputResolutionWidth"] == "3840"
    b.timeline = c
    b.project.GetCurrentTimeline = lambda: c
    b.refresh_timeline()
    b.autoscale_tick()
    assert a.d["timelineOutputResolutionWidth"] == "5760" and a.d["timelineOutputResMatchTimelineRes"] == "1"
    assert c.d["timelineOutputResolutionWidth"] == "3840"
    assert list(b.scaled) == ["b"]


def test_autoscale_waits_for_the_timeline_s_own_project(tmp_path):
    """Nelson, 20 Nov: with the headset connected he opened another project; its timeline was scaled, and once he
    had moved on the record named the wrong project and the bridge logged 'not found' every two seconds."""
    a = _Tl("Strange Worlds Teaser v1", SQUARE, uid="sw")
    b, _ = _scaling_bridge(tmp_path, a)
    b.project.name = "Strange Worlds Teaser copy"
    b.clients = [object()]
    b.autoscale_tick()
    assert a.d["timelineOutputResolutionWidth"] == "3840" and b.scaled["sw"]["project"] == "Strange Worlds Teaser copy"
    # the user opens a different project (the timeline is no longer reachable) and the headset leaves
    other = _Tl("Other", {"timelineResolutionWidth": "1920", "timelineResolutionHeight": "1080"}, uid="o")
    b.project = _project(other, {})
    b.project.name = "2608 - BC Salmon"
    b.timeline = other
    b.refresh_timeline()
    b.clients = []
    with open(vb.LOG) as f: before = f.read().count("is not open")
    b.autoscale_tick(now=1.0); b.autoscale_tick(now=10.0); b.autoscale_tick(now=20.0); b.autoscale_tick(now=30.0)
    with open(vb.LOG) as f: after = f.read().count("is not open")
    assert after - before == 1                       # said once
    assert list(b.scaled) == ["sw"] and json.load(open(vb.SIDECAR))["scaled"]["sw"]["project"] == "Strange Worlds Teaser copy"
    assert a.d["timelineOutputResolutionWidth"] == "3840"   # untouched: we cannot reach it from here
    # back in the right project: restored
    b.project = _project(a, {})
    b.project.name = "Strange Worlds Teaser copy"
    b.timeline = a
    b.refresh_timeline()
    b.autoscale_tick()
    assert a.d["timelineOutputResolutionWidth"] == "5760" and a.d["timelineOutputResMatchTimelineRes"] == "1"
    assert not b.scaled and not os.path.exists(vb.SIDECAR)


def test_driver_notice_when_blackmagic_is_back_on_top(tmp_path, monkeypatch):
    active = tmp_path / "Info.plist"
    aside = tmp_path / "aside"
    monkeypatch.setattr(vb, "FW_ACTIVE", str(active))
    monkeypatch.setattr(vb, "FW_ASIDE", (str(aside),))
    vb._driver_check.update(at=0.0, msg=None)
    # nothing set aside: whatever is active, no notice
    active.write_text("<plist>com.blackmagic-design</plist>")
    assert vb.driver_notice(now=1000) is None
    # set aside + Blackmagic active = Desktop Video was installed over Velaria
    aside.mkdir()
    assert "replaced Velaria" in vb.driver_notice(now=2000)
    # cached for 30 s, then rechecked
    active.write_text("<plist>ca.skylightproductions.velaria</plist>")
    assert vb.driver_notice(now=2010) is not None
    assert vb.driver_notice(now=2040) is None
