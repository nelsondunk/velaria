"""Build an Edit-page drop-on card: Velaria Canvas Control + one Velaria 3D Title, pre-wired, in a single macro.

The plate (MainInput1) feeds a PipeRouter that goes to both the title's Environment and Canvas Control's Footage;
the title's scene goes to Canvas Control's 3D Scene; "Window shows" defaults to 2 (nothing: 3D only).
Published: Canvas Control's Placement / Stereo pages + Window shows, then every title control.

Usage: python3 make_card_combo.py "Velaria 3D Title - Vine.setting" Vine "Velaria 3D Card - Vine.setting"
"""
import sys

import settingparse as sp
import settingwrite as sw

title_path, label, dst = sys.argv[1:4]
rc, _ = sp.load("Velaria Canvas Control.setting")
ccname, cc, _ = sp.tools_of(rc)
dc = sp.py(rc)
cc = dc["Tools"][ccname]
rt, _ = sp.load(title_path)
tname, tt, _ = sp.tools_of(rt)
dt = sp.py(rt)
tt = dt["Tools"][tname]

tools = {}
tools.update(cc["Tools"])
assert not (set(tools) & set(tt["Tools"])), set(tools) & set(tt["Tools"])
tools.update(tt["Tools"])

# plate router -> title environment + canvas footage
tools["Plate_In"] = {"__type": "PipeRouter", "ViewInfo": {"__type": "PipeRouterInfo", "Pos": [-2300, -395]}}
tools["FB_EnvSquare"]["Inputs"]["Input"] = {"__type": "Input", "SourceOp": "Plate_In", "Source": "Output"}
tools["Left_Footage_Input"]["Inputs"]["Input"] = {"__type": "Input", "SourceOp": "Plate_In", "Source": "Output"}
# title scene -> canvas 3D scene
tools["Scene_Offset"]["Inputs"]["SceneInput"] = {"__type": "Input", "SourceOp": "FB_Scene", "Source": "Output"}
# window shows nothing by default (3D only)
tools["Velaria_Controls"]["Inputs"]["WindowShows"] = {"__type": "Input", "Value": 2}
# composite the finished over/under card back over the plate, as the old Fact Cards did (FB_CardOverPlate),
# so the macro works dropped straight onto the plate clip or an adjustment clip
tools["Card_Over_Plate"] = {
    "__type": "Merge",
    "ViewInfo": {"__type": "OperatorInfo", "Pos": [1200, -395]},
    "Inputs": {
        "Background": {"__type": "Input", "SourceOp": "Plate_In", "Source": "Output"},
        "Foreground": {"__type": "Input", "SourceOp": "Stack_Eyes_OverUnder", "Source": "Output"},
        "PerformDepthMerge": {"__type": "Input", "Value": 0},
    },
}

inputs = {"__ordered": True}
inputs["MainInput1"] = {"__type": "InstanceInput", "SourceOp": "Plate_In", "Source": "Input", "Name": "Plate (360 footage)"}
cci = cc["Inputs"]
for key in ("Yaw", "Pitch", "Roll", "Distance", "ObjectTurn", "ObjectTilt", "ObjectSpin"):
    inputs[key] = dict(cci[key])
for key in ("Mode", "WindowDepth", "OutputWidth"):
    inputs[key] = dict(cci[key])
TITLE_ORDER = ["TitleText", "BodyText", "CardScale", "ModelShow", "ModelKit", "ModelScale", "ModelX", "ModelY", "ModelZ",
               "ModelRotX", "ModelRotY", "ModelRotZ", "ModelBright", "ModelContrast", "ModelSat",
               "AnimStart", "AnimSpeed", "HoldSeconds", "Sway", "EnvOverUnder"]
for key in TITLE_ORDER:
    inputs[key] = dict(tt["Inputs"][key])
rest = [k for k in tt["Inputs"] if k not in TITLE_ORDER and k not in ("__ordered", "MainInput1")]
assert not rest, rest

macro = {
    "__type": "MacroOperator",
    "Inputs": inputs,
    "Outputs": {
        "MainOutput1": {"__type": "InstanceOutput", "SourceOp": "Card_Over_Plate", "Source": "Output", "Name": "Card over plate"},
        "MainOutput2": {"__type": "InstanceOutput", "SourceOp": "Stack_Eyes_OverUnder", "Source": "Output", "Name": "Card only (over/under, transparent)"},
    },
    "Tools": tools,
}
if "ViewInfo" in cc:
    macro["ViewInfo"] = cc["ViewInfo"]
d = {"Tools": {"Velaria3DCard_" + label.replace(" ", ""): macro}}
sw.write_setting(d, dst)

names = set(tools)
bad = [(n, i, so) for n, t in tools.items() for i, so, s in sp.links(t) if so not in names]
print(f"{dst}: tools {len(tools)}, inputs {len(inputs)-1}, bad links {bad}")
