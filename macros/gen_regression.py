#!/usr/bin/env python3
"""Generate one Resolve Fusion comp per Kalyx macro variant for the render regression (weekend task H).

Each comp: MediaIn1 (the clip) -> macro instance(s) -> MediaOut1, with the published inputs set for the variant.
Output: regression/<id>.comp plus regression/variants.json describing them. Import with TimelineItem.ImportFusionComp.
"""
import copy
import json
import os
import sys

import settingparse as sp
import settingwrite as sw

OUT = sys.argv[1] if len(sys.argv) > 1 else "regression"
os.makedirs(OUT, exist_ok=True)
RANGE = 59   # 60-frame clips


def macro(path):
    r, _ = sp.load(path)
    name, t, _ = sp.tools_of(r)
    return name, copy.deepcopy(sp.py(r)["Tools"][name])


def set_inner(m, instance_input, value):
    """Set a published input's value on the inner tool it points at (that is where a comp stores it)."""
    ii = m["Inputs"][instance_input]
    src, inp = ii["SourceOp"], ii["Source"]
    tool = m["Tools"][src]
    tool.setdefault("Inputs", {})
    cur = tool["Inputs"].get(inp)
    if isinstance(cur, dict):
        cur = dict(cur)
        cur.pop("Expression", None)
        cur["Value"] = value
        cur["__type"] = "Input"
    else:
        cur = {"__type": "Input", "Value": value}
    tool["Inputs"][inp] = cur
    ii["Default"] = value


def connect_inner(m, instance_input, src_op, src_out):
    ii = m["Inputs"][instance_input]
    tool = m["Tools"][ii["SourceOp"]]
    tool.setdefault("Inputs", {})[ii["Source"]] = {"__type": "Input", "SourceOp": src_op, "Source": src_out}


def out_of(m, output_name):
    """A macro's published output as (inner tool, output): comp files must reference the inner tool, a reference to
    MacroName.MainOutput1 imports but renders nothing (task H, 31 Oct)."""
    o = m["Outputs"][output_name]
    return o["SourceOp"], o["Source"]


def comp(tools_ordered, out_src, out_name):
    tools = {"__ordered": True}
    tools["MediaIn1"] = {"__type": "MediaIn", "Inputs": {"Layer": {"__type": "Input", "Value": "0"}, "GlobalIn": {"__type": "Input", "Value": 0}, "GlobalOut": {"__type": "Input", "Value": RANGE}},
                         "ViewInfo": {"__type": "OperatorInfo", "Pos": [-500, 0]}}
    for k, v in tools_ordered:
        tools[k] = v
    tools["MediaOut1"] = {"__type": "MediaOut", "Inputs": {"Index": {"__type": "Input", "Value": "0"}, "Input": {"__type": "Input", "SourceOp": out_src, "Source": out_name}},
                          "ViewInfo": {"__type": "OperatorInfo", "Pos": [600, 0]}}
    return {"__type": "Composition", "CurrentTime": 0, "RenderRange": [0, RANGE], "GlobalRange": [0, RANGE], "HiQ": True, "PlaybackUpdateMode": 0,
                            "Version": "DaVinci Resolve Studio 21.1.0.0017", "SavedOutputs": 1, "Resumable": True, "Tools": tools,
                            "Frames": {}, "Prefs": {"Comp": {"FrameFormat": {"Width": 5760, "Height": 2880, "Rate": 23.976023976024, "GuideRatio": 2.0, "DepthFull": 3, "DepthPreview": 3, "DepthInteractive": 3},
                                                    "Unsorted": {"GlobalEnd": RANGE}}}}


variants = []
KIT_OVERRIDE = {"Salmon": "/Users/nelsondunk/kalyx-test/kits/Salmon_Kit"}   # NYC has no Earth-1610; the kit is copied there


def kit(m, label):
    if label in KIT_OVERRIDE:
        set_inner(m, "ModelKit", KIT_OVERRIDE[label])


def emit_comp(vid, c, desc):
    path = os.path.join(OUT, vid + ".comp")
    sw.write_setting(c, path)
    variants.append({"id": vid, "desc": desc, "file": os.path.basename(path)})


# ---- Canvas Control alone: Window shows 0/1/2, footage + graphic both from the clip
ccname, _ = macro("Velaria Canvas Control.setting")
for ws in (0, 1, 2):
    _, m = macro("Velaria Canvas Control.setting")
    set_inner(m, "Source", ws)
    connect_inner(m, "MainInput1", "MediaIn1", "Output")
    connect_inner(m, "MainInput3", "MediaIn1", "Output")
    emit_comp("cc_ws%d" % ws, comp([(ccname, m)], *out_of(m, "MainOutput1")), "Velaria Canvas Control, Window shows = %d, footage + graphic = clip" % ws)

# ---- 3D Titles through Canvas Control's 3D Scene input (the only way a title becomes pixels)
for label in ("Vine", "Salmon", "Hay Bale"):
    tpath = "Velaria 3D Title - %s.setting" % label
    tname, t = macro(tpath)
    kit(t, label)
    _, m = macro("Velaria Canvas Control.setting")
    set_inner(m, "Source", 2)
    connect_inner(m, "MainInput1", "MediaIn1", "Output")
    connect_inner(t, "MainInput1", "MediaIn1", "Output")
    connect_inner(m, "SceneInput", *out_of(t, "MainOutput1"))
    emit_comp("title_%s" % label.replace(" ", ""), comp([(tname, t), (ccname, m)], *out_of(m, "MainOutput1")), "Velaria 3D Title - %s -> Canvas Control 3D Scene, Window shows = 2" % label)

# ---- 3D Cards: Window shows 0/1/2 at defaults, then fade (HoldSeconds 3), then Environment not over/under
for label in ("Vine", "Salmon", "Hay Bale"):
    cpath = "Velaria 3D Card - %s.setting" % label
    short = label.replace(" ", "")
    for ws in (0, 1, 2):
        cname, c = macro(cpath)
        kit(c, label)
        connect_inner(c, "MainInput1", "MediaIn1", "Output")
        # the card has no published Window shows; set the inner control directly
        c["Tools"]["Velaria_Controls"]["Inputs"]["WindowShows"] = {"__type": "Input", "Value": ws}
        emit_comp("card_%s_ws%d" % (short, ws), comp([(cname, c)], *out_of(c, "MainOutput1")), "Velaria 3D Card - %s, Window shows = %d" % (label, ws))
    cname, c = macro(cpath)
    kit(c, label)
    connect_inner(c, "MainInput1", "MediaIn1", "Output")
    set_inner(c, "HoldSeconds", 1)
    emit_comp("card_%s_hold1" % short, comp([(cname, c)], *out_of(c, "MainOutput1")), "Velaria 3D Card - %s, fade out after 1 s (frame 40 is mid-fade)" % label)
    cname, c = macro(cpath)
    kit(c, label)
    connect_inner(c, "MainInput1", "MediaIn1", "Output")
    set_inner(c, "EnvOverUnder", 0)
    emit_comp("card_%s_env0" % short, comp([(cname, c)], *out_of(c, "MainOutput1")), "Velaria 3D Card - %s, environment not over/under" % label)
    cname, c = macro(cpath)
    kit(c, label)
    connect_inner(c, "MainInput1", "MediaIn1", "Output")
    emit_comp("card_%s_out2" % short, comp([(cname, c)], *out_of(c, "MainOutput2")), "Velaria 3D Card - %s, output 2 (card only, transparent)" % label)

# ---- missing kit folder
cname, c = macro("Velaria 3D Card - Vine.setting")
connect_inner(c, "MainInput1", "MediaIn1", "Output")
set_inner(c, "ModelKit", "/Volumes/Nowhere/Vine_Kit")
emit_comp("card_Vine_missingkit", comp([(cname, c)], *out_of(c, "MainOutput1")), "Velaria 3D Card - Vine with the kit folder missing")

# ---- Orient 360
oname, o = macro("Velaria Orient 360.setting")
connect_inner(o, "MainInput1", "MediaIn1", "Output")
set_inner(o, "Pan", 30)
emit_comp("orient360", comp([(oname, o)], *out_of(o, "MainOutput1")), "Velaria Orient 360, pan 30")

# ---- Trim (task D): warm, lifted, a little contrast
tname, t = macro("Velaria Trim.setting")
connect_inner(t, "MainInput1", "MediaIn1", "Output")
set_inner(t, "Temperature", 0.6)
set_inner(t, "Lift", 0.05)
set_inner(t, "Contrast", 1.2)
emit_comp("trim_warm", comp([(tname, t)], *out_of(t, "MainOutput1")), "Velaria Trim, temperature +0.6, lift +0.05, contrast 1.2")

json.dump(variants, open(os.path.join(OUT, "variants.json"), "w"), indent=1)
print(len(variants), "comps in", OUT)
