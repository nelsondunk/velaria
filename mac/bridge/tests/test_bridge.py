"""Bridge tests against the fake Resolve: run with `python3 -m pytest mac/bridge/tests` or plain `python3 test_bridge.py`.

They cover what the headset relies on: the timeline snapshot, the params message (sections, names, colour triplets,
virtual Scale/card-rotation controls, switch detection, sticky ranges), set/undo/paste round trips, keyframes at the
playhead's comp time, the Fusion-page playhead fallback and the transient-None guard.
"""
import os
import sys
import types

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
    # layers: a MacroOperator is a card, PanoMap / Kalyx_Orient* are orient, LatLongPatcher is a patch; tools
    # inside a macro (Orient 360's two PanoMaps) never become groups of their own
    b, _ = make_bridge()
    inner = [fake_resolve.pano_map("PanoMap_L"), fake_resolve.pano_map("PanoMap_R")]
    orient = fake_resolve.Tool("Kalyx_Orient360", "MacroOperator", [
        ("Pan", "Pan", "Number", "ScrewControl", -180.0, 180.0, 4.0)], children=inner)
    patch = fake_resolve.Tool("LatLongPatcher1", "LatLongPatcher", [
        ("Rotate.X", "Rotate X", "Number", "ScrewControl", -180.0, 180.0, 90.0)])
    plate = b.timeline.plate_comp
    plate.tools.extend([orient] + inner + [patch])
    msg = b.macro_params()
    kinds = {g["tool"]: g.get("kind") for g in msg["groups"]}
    assert kinds["Skylight_FactWindow"] == "card"
    assert kinds["PanoMap1"] == "orient"
    assert kinds["Kalyx_Orient360"] == "orient"
    assert kinds["LatLongPatcher1"] == "patch"
    assert "PanoMap_L" not in kinds and "PanoMap_R" not in kinds
    # cards first, then the rest
    order = [g.get("kind") for g in msg["groups"]]
    assert order[0] == "card" and "card" not in order[1:]


def test_trim_macro_is_kind_trim_with_named_controls_in_a_trim_tab():
    # Kalyx Trim (ColorCorrector on an adjustment clip) grades the whole picture: kind "trim", after cards and orient,
    # before patches; its controls keep their names and land in one Trim section
    b, _ = make_bridge()
    trim = fake_resolve.Tool("Kalyx_Trim", "MacroOperator", [
        ("Exposure", "Exposure (offset)", "Number", "SliderControl", -0.5, 0.5, 0.0),
        ("Gamma", "Gamma", "Number", "SliderControl", 0.5, 2.0, 1.0),
        ("Temperature", "Temperature (blue - amber)", "Number", "SliderControl", -1.0, 1.0, 0.0),
        ("Amount", "Amount (0 = off)", "Number", "SliderControl", 0.0, 1.0, 1.0)])
    patch = fake_resolve.Tool("LatLongPatcher1", "LatLongPatcher", [
        ("Rotate.X", "Rotate X", "Number", "ScrewControl", -180.0, 180.0, 90.0)])
    b.timeline.plate_comp.tools.extend([trim, patch])
    msg = b.macro_params()
    g = {x["tool"]: x for x in msg["groups"]}
    assert g["Kalyx_Trim"]["kind"] == "trim"
    order = [x["kind"] for x in msg["groups"]]
    assert order.index("trim") > order.index("card") and order.index("trim") < order.index("patch")
    names = {p["id"]: p["name"] for p in g["Kalyx_Trim"]["params"]}
    assert names["Exposure"] == "Exposure" and names["Temperature"] == "Temperature" and names["Amount"] == "Amount"
    assert {p.get("section") for p in g["Kalyx_Trim"]["params"]} == {"Trim"}


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
