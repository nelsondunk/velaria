#!/usr/bin/env python3
"""Comps for rendering the Velaria title templates through Resolve on the 360 chart (regression-rig/s_titles.py):
MediaIn1 (the chart) -> template macro -> MediaOut1, one comp per template, plus the Clean one with "Animate out"
off and the Slab one with the slab off. Output: titles/<id>.comp + variants.json."""
import copy
import json
import os
import sys

import settingparse as sp
import settingwrite as sw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "titles")
os.makedirs(OUT, exist_ok=True)
RANGE = 149   # a 5 s still at 30 fps


def macro(path):
    r, _ = sp.load(os.path.join(HERE, path))
    name, t, _ = sp.tools_of(r)
    return name, copy.deepcopy(sp.py(r)["Tools"][name])


def set_inner(m, key, value):
    ii = m["Inputs"][key]
    tool = m["Tools"][ii["SourceOp"]]
    cur = dict(tool.setdefault("Inputs", {}).get(ii["Source"], {"__type": "Input"}))
    cur.pop("Expression", None)
    cur["Value"] = value
    cur["__type"] = "Input"
    tool["Inputs"][ii["Source"]] = cur
    ii["Default"] = value


def comp(name, m):
    tools = {"__ordered": True}
    tools["MediaIn1"] = {"__type": "MediaIn", "Inputs": {"Layer": {"__type": "Input", "Value": "0"}, "GlobalIn": {"__type": "Input", "Value": 0}, "GlobalOut": {"__type": "Input", "Value": RANGE}},
                         "ViewInfo": {"__type": "OperatorInfo", "Pos": [-500, 0]}}
    m["Tools"]["Plate_In"]["Inputs"] = {"Input": {"__type": "Input", "SourceOp": "MediaIn1", "Source": "Output"}}
    tools[name] = m
    tools["MediaOut1"] = {"__type": "MediaOut", "Inputs": {"Index": {"__type": "Input", "Value": "0"}, "Input": {"__type": "Input", "SourceOp": "Card_Over_Plate", "Source": "Output"}},
                          "ViewInfo": {"__type": "OperatorInfo", "Pos": [600, 0]}}
    return {"__type": "Composition", "CurrentTime": 0, "RenderRange": [0, RANGE], "GlobalRange": [0, RANGE], "HiQ": True, "PlaybackUpdateMode": 0,
            "Version": "DaVinci Resolve Studio 21.1.0.0017", "SavedOutputs": 1, "Resumable": True, "Tools": tools, "Frames": {},
            "Prefs": {"Comp": {"FrameFormat": {"Width": 3840, "Height": 3840, "Rate": 30, "GuideRatio": 1.0, "DepthFull": 3, "DepthPreview": 3, "DepthInteractive": 3},
                               "Unsorted": {"GlobalEnd": RANGE}}}}


variants = []


def emit(vid, name, m, desc, frames):
    path = os.path.join(OUT, vid + ".comp")
    sw.write_setting(comp(name, m), path)
    variants.append({"id": vid, "desc": desc, "file": os.path.basename(path), "timeline": "14 Titles 360", "frames": frames})


for label, fname in (("clean", "Velaria Title - Clean.setting"), ("lower", "Velaria Lower Third.setting"), ("caption", "Velaria Caption.setting"), ("slab", "Velaria Title 3D - Slab.setting")):
    name, m = macro(fname)
    set_inner(m, "Yaw", 30)   # off the chart's centre word so the text reads against a plain patch
    set_inner(m, "TitleText", {"clean": "Harvest at first light", "lower": "Nelson Dunk", "caption": "Elmira, Ontario", "slab": "VELARIA"}[label])
    if "SubText" in m["Inputs"]:
        set_inner(m, "SubText", {"clean": "A line about it, set small", "lower": "Director of photography"}[label])
    emit(label, name, m, fname + " at defaults, yaw 30", [6, 40, 145])

name, m = macro("Velaria Title - Clean.setting")
set_inner(m, "Yaw", 30); set_inner(m, "AnimateOut", 0); set_inner(m, "TitleText", "Stays until the cut")
emit("clean_noout", name, m, "Clean, Animate out off: frame 145 must be as frame 40", [145])
name, m = macro("Velaria Title 3D - Slab.setting")
set_inner(m, "Yaw", 30); set_inner(m, "SlabOn", 0); set_inner(m, "TitleText", "NO SLAB")
emit("slab_off", name, m, "Slab off: letters only, lit", [40])

json.dump(variants, open(os.path.join(OUT, "variants.json"), "w"), indent=1)
print("wrote", len(variants), "comps to", OUT)
