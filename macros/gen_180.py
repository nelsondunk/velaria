#!/usr/bin/env python3
"""Comps for the 180 / stereo macro checks on the "Velaria Format Tests" project (chart timelines 12 and 13).
Each comp: MediaIn1 (the chart) -> macro(s) -> MediaOut1. Output: fmt180/<id>.comp + variants.json
(id, timeline, file, desc, expect). Same conventions as gen_regression.py: macro outputs referenced by inner tool."""
import copy, json, os, sys
import settingparse as sp, settingwrite as sw

OUT = sys.argv[1] if len(sys.argv) > 1 else "fmt180"
os.makedirs(OUT, exist_ok=True)
RANGE = 59

def macro(path):
    r, _ = sp.load(path)
    name, t, _ = sp.tools_of(r)
    return name, copy.deepcopy(sp.py(r)["Tools"][name])

def set_inner(m, instance_input, value):
    ii = m["Inputs"][instance_input]
    tool = m["Tools"][ii["SourceOp"]]
    tool.setdefault("Inputs", {})
    cur = tool["Inputs"].get(ii["Source"])
    cur = dict(cur) if isinstance(cur, dict) else {"__type": "Input"}
    cur.pop("Expression", None); cur["Value"] = value; cur["__type"] = "Input"
    tool["Inputs"][ii["Source"]] = cur
    ii["Default"] = value

def connect_inner(m, instance_input, src_op, src_out):
    ii = m["Inputs"][instance_input]
    m["Tools"][ii["SourceOp"]].setdefault("Inputs", {})[ii["Source"]] = {"__type": "Input", "SourceOp": src_op, "Source": src_out}

def out_of(m, output_name="MainOutput1"):
    o = m["Outputs"][output_name]
    return o["SourceOp"], o["Source"]

def comp(tools_ordered, out_src, out_name, w, h):
    tools = {"__ordered": True}
    tools["MediaIn1"] = {"__type": "MediaIn", "Inputs": {"Layer": {"__type": "Input", "Value": "0"}, "GlobalIn": {"__type": "Input", "Value": 0}, "GlobalOut": {"__type": "Input", "Value": RANGE}},
                         "ViewInfo": {"__type": "OperatorInfo", "Pos": [-700, 0]}}
    for k, v in tools_ordered:
        tools[k] = v
    tools["MediaOut1"] = {"__type": "MediaOut", "Inputs": {"Index": {"__type": "Input", "Value": "0"}, "Input": {"__type": "Input", "SourceOp": out_src, "Source": out_name}},
                          "ViewInfo": {"__type": "OperatorInfo", "Pos": [800, 0]}}
    return {"__type": "Composition", "CurrentTime": 0, "RenderRange": [0, RANGE], "GlobalRange": [0, RANGE], "HiQ": True, "PlaybackUpdateMode": 0,
            "Version": "DaVinci Resolve Studio 21.1.0.0017", "SavedOutputs": 1, "Resumable": True, "Tools": tools, "Frames": {},
            "Prefs": {"Comp": {"FrameFormat": {"Width": w, "Height": h, "Rate": 30, "GuideRatio": w / h, "DepthFull": 3, "DepthPreview": 3, "DepthInteractive": 3},
                               "Unsorted": {"GlobalEnd": RANGE}}}}

variants = []
def emit(vid, timeline, c, desc, expect):
    path = os.path.join(OUT, vid + ".comp")
    sw.write_setting(c, path)
    variants.append({"id": vid, "timeline": timeline, "file": os.path.basename(path), "desc": desc, "expect": expect})

T180, T360 = "12 Chart 180 SBS", "13 Chart 360 TB"
W180, H180, W360, H360 = 3840, 1920, 3840, 3840

# 1. 180 -> 360 alone (what the padded frame looks like), and the round trip
n1, m1 = macro("Velaria 180 to 360.setting")
connect_inner(m1, "MainInput1", "MediaIn1", "Output")
emit("pad_180to360", T180, comp([(n1, m1)], *out_of(m1), W180, H180), "180 SBS -> padded 360 TB (rendered into the 2:1 frame, so it will be squashed: geometry only)",
     "each eye centred in a 2:1 row, black either side; L row on top")
n1, m1 = macro("Velaria 180 to 360.setting"); connect_inner(m1, "MainInput1", "MediaIn1", "Output")
n2, m2 = macro("Velaria 360 to 180.setting"); connect_inner(m2, "MainInput1", *out_of(m1))
emit("roundtrip_180", T180, comp([(n1, m1), (n2, m2)], *out_of(m2), W180, H180), "180 -> 360 -> 180", "pixel-identical to the chart")

# 2. Orient 180: tilt and roll
for pid, inp, val in (("or180_tilt10", "Tilt", 10), ("or180_roll10", "Roll", 10)):
    n, m = macro("Velaria Orient 180.setting"); connect_inner(m, "MainInput1", "MediaIn1", "Output"); set_inner(m, inp, val)
    emit(pid, T180, comp([(n, m)], *out_of(m), W180, H180), "Orient 180 %s %d" % (inp, val), "both eyes move the same way; compare with ffmpeg v360 hequirect pitch/roll")

# 3. Stereo convergence and swap
for pid, fname, tl, w, h, inp, val in (("st180_conv", "Velaria Stereo 180.setting", T180, W180, H180, "Convergence", 0.02),
                                       ("st180_swap", "Velaria Stereo 180.setting", T180, W180, H180, "SwapEyes", 1),
                                       ("st360_conv", "Velaria Stereo 360.setting", T360, W360, H360, "Convergence", 0.02),
                                       ("st360_swap", "Velaria Stereo 360.setting", T360, W360, H360, "SwapEyes", 1)):
    n, m = macro(fname); connect_inner(m, "MainInput1", "MediaIn1", "Output"); set_inner(m, inp, val)
    emit(pid, tl, comp([(n, m)], *out_of(m), w, h), "%s %s=%s" % (n, inp, val),
         "conv: eye1 content shifts +1% of eye width, eye2 -1% (opposite); swap: L and R exchange")

# 4. Canvas Control card on a 180 plate via the 360 -> 180 adapter, merged over the plate; and the 360 reference
def cc(card_w=3840):
    n, m = macro("Velaria Canvas Control.setting")
    connect_inner(m, "MainInput1", "MediaIn1", "Output")   # the window shows the chart itself: fine for geometry
    set_inner(m, "Mode", 1)             # stereo
    set_inner(m, "OutputWidth", card_w)
    return n, m
n, m = cc(); n2, m2 = macro("Velaria 360 to 180.setting"); connect_inner(m2, "MainInput1", *out_of(m))
merge = {"__type": "Merge", "Inputs": {"Background": {"__type": "Input", "SourceOp": "MediaIn1", "Source": "Output"},
                                        "Foreground": {"__type": "Input", "SourceOp": out_of(m2)[0], "Source": out_of(m2)[1]}},
         "ViewInfo": {"__type": "OperatorInfo", "Pos": [600, 0]}}
emit("cc180_centre", T180, comp([(n, m), (n2, m2), ("Over180", merge)], "Over180", "Output", W180, H180),
     "Canvas Control (stereo, default placement) -> 360 to 180 -> over the 180 chart", "a card at the centre of both eyes, same size in each, slightly offset for depth")
n, m = cc(); set_inner(m, "Yaw", 60)
n2, m2 = macro("Velaria 360 to 180.setting"); connect_inner(m2, "MainInput1", *out_of(m))
merge = copy.deepcopy(merge); merge["Inputs"]["Foreground"] = {"__type": "Input", "SourceOp": out_of(m2)[0], "Source": out_of(m2)[1]}
emit("cc180_yaw60", T180, comp([(n, m), (n2, m2), ("Over180", merge)], "Over180", "Output", W180, H180),
     "same, Yaw 60", "card centred at the 60-degree line of each eye (two thirds of the way to the right edge)")
n, m = cc()
merge = {"__type": "Merge", "Inputs": {"Background": {"__type": "Input", "SourceOp": "MediaIn1", "Source": "Output"},
                                        "Foreground": {"__type": "Input", "SourceOp": out_of(m)[0], "Source": out_of(m)[1]}},
         "ViewInfo": {"__type": "OperatorInfo", "Pos": [600, 0]}}
emit("cc360_centre", T360, comp([(n, m), ("Over360", merge)], "Over360", "Output", W360, H360),
     "Canvas Control (stereo, default) over the 360 chart: the reference", "card at the centre of both eyes")

json.dump(variants, open(os.path.join(OUT, "variants.json"), "w"), indent=1)
print(len(variants), "comps in", OUT)
