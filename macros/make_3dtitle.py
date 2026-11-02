"""Convert a Skylight Fact Card 360 macro (self-contained 360 renderer) into a Kalyx 3D Title macro:
a 3D scene (plates, extruded text, model, lights) that plugs into Kalyx Canvas Control's "3D Scene" input.
Canvas Control owns placement; the title owns text, model, animation and the environment reflections.

Usage: python3 make_3dtitle.py "factcards/Skylight Fact Card 360 - Vine.setting" "Kalyx 3D Title - Vine.setting" Vine
"""
import sys

import settingparse as sp
import settingwrite as sw

src, dst, label = sys.argv[1:4]
root, L = sp.load(src)
mname, macro, tools = sp.tools_of(root)
d = sp.py(root)
macro = d["Tools"][mname]
tools = macro["Tools"]

# ---- 1. the placement / render / composite chain goes: Canvas Control does this now
DROP = {
    "FB_PlaceL", "FB_PlaceR", "FB_OrbitL", "FB_OrbitR", "FB_CamL", "FB_CamR", "FB_MergeL", "FB_MergeR",
    "FB_RendererL", "FB_RendererR", "FB_Combiner", "FB_CardOverPlate",
    # leftovers that were never wired to anything
    "Loader1", "Tracker1", "Merge1", "Note1", "Left", "Right",
    # the environment probe computed averages nobody read
    "FB_EnvProbe", "FB_EnvProbe_Red", "FB_EnvProbe_Green", "FB_EnvProbe_Blue",
    "CustomTool1LUTIn11", "CustomTool1LUTIn21", "CustomTool1LUTIn31", "CustomTool1LUTIn41",
    # footage router + fixed-pixel crop, replaced by a size-independent chain below
    "FB_FootageIn", "FB_EnvLeftEye",
}
for n in list(tools):
    if n in DROP:
        del tools[n]

# ---- 2. environment chain: plate -> 1024x1024 -> (top eye if over/under) -> 1024x512 -> blur -> sphere map
tools["FB_EnvSquare"] = {
    "__type": "BetterResize",
    "ViewInfo": {"__type": "OperatorInfo", "Pos": [1009, 47.68]},
    "Inputs": {
        "Width": {"__type": "Input", "Value": 1024},
        "Height": {"__type": "Input", "Value": 1024},
        "PixelAspect": {"__type": "Input", "Value": [1, 1]},
        "HiQOnly": {"__type": "Input", "Value": 0},
    },
}
tools["FB_EnvTopEye"] = {
    "__type": "Crop",
    "ViewInfo": {"__type": "OperatorInfo", "Pos": [1155, 45.58]},
    "Inputs": {
        "Input": {"__type": "Input", "SourceOp": "FB_EnvSquare", "Source": "Output"},
        "XOffset": {"__type": "Input", "Value": 0},
        "XSize": {"__type": "Input", "Value": 1024},
        "YOffset": {"__type": "Input", "Expression": "iif(CARD_CONTROLS.EnvOverUnder > 0.5, 512, 0)"},
        "YSize": {"__type": "Input", "Expression": "iif(CARD_CONTROLS.EnvOverUnder > 0.5, 512, 1024)"},
    },
}
tools["FB_EnvSmall"]["Inputs"]["Input"] = {"__type": "Input", "SourceOp": "FB_EnvTopEye", "Source": "Output"}

# ---- 3. controls: placement controls leave, environment + fade-out arrive
cc = tools["CARD_CONTROLS"]
uc = cc["UserControls"]
for k in ("Yaw", "Pitch", "Distance", "CardRoll", "CardTurn", "CardTip"):
    uc.pop(k, None)
    cc.get("Inputs", {}).pop(k, None)
uc["CardScale"]["LINKS_Name"] = "Title scale (1 = as designed)"
uc["CardScale"]["ICS_ControlPage"] = "Text"
uc["EnvOverUnder"] = {
    "LINKID_DataType": "Number", "INPID_InputControl": "CheckboxControl", "INP_Default": 1,
    "ICS_ControlPage": "Environment", "LINKS_Name": "Environment is over/under (left eye on top)",
}
uc["FadeOut"] = {
    "LINKID_DataType": "Number", "INPID_InputControl": "SliderControl", "INP_Default": 1,
    "INP_MinScale": 0, "INP_MaxScale": 1, "IC_Visible": False,
    "ICS_ControlPage": "Animation", "LINKS_Name": "Fade out (driven)",
}
cc.setdefault("Inputs", {})
cc["Inputs"]["EnvOverUnder"] = {"__type": "Input", "Value": 1}
cc["Inputs"]["FadeOut"] = {
    "__type": "Input",
    "Expression": "iif(CARD_CONTROLS.HoldSeconds <= 0, 1, 1-math.min(1,math.max(0,(time-CARD_CONTROLS.AnimStart-60*CARD_CONTROLS.AnimSpeed-CARD_CONTROLS.HoldSeconds*30)/(18*CARD_CONTROLS.AnimSpeed))))",
}
# the self-fade is opt-in now: 0 = the card stays for the length of the clip
uc["HoldSeconds"]["INP_Default"] = 0
uc["HoldSeconds"]["INP_MinScale"] = 0
uc["HoldSeconds"]["LINKS_Name"] = "Fade out after (seconds, 0 = never)"
cc["Inputs"]["HoldSeconds"] = {"__type": "Input", "Value": 0}

# ---- 4. expressions: no card turn/tip (Canvas Control does it); fade-out multiplies every opacity
scene = tools["FB_Scene"]["Inputs"]
scene["Transform3DOp.Rotate.Y"] = {"__type": "Input", "Expression": "CARD_CONTROLS.Sway*3.5*math.sin(time*2*math.pi/240)"}
scene["Transform3DOp.Rotate.X"] = {"__type": "Input", "Expression": "CARD_CONTROLS.Sway*1.5*math.sin(time*2*math.pi/310)"}


def fade(tool, inp):
    iv = tools[tool]["Inputs"][inp]
    assert "Expression" in iv, (tool, inp)
    iv["Expression"] = "CARD_CONTROLS.FadeOut*(" + iv["Expression"] + ")"


scene["Transform3DOp.Scale.X"] = {"__type": "Input", "Expression": "CARD_CONTROLS.CardScale*(1-0.12*(1-CARD_CONTROLS.FadeOut))"}
# MtlReflect's reflection layer ignores the background material's opacity, so fade its strength as well
for n, t in tools.items():
    if t.get("__type") == "MtlReflect":
        for inp in ("Reflection.FaceOnStrength", "Reflection.GlancingStrength"):
            if inp in t.get("Inputs", {}) and "Expression" in t["Inputs"][inp]:
                fade(n, inp)
            else:
                v = t["Inputs"].get(inp, {}).get("Value", 1)
                t["Inputs"][inp] = {"__type": "Input", "Expression": f"CARD_CONTROLS.FadeOut*{v}"}
fade("FB_Title", "Opacity1")
fade("FB_Body", "Opacity1")
for n, t in tools.items():
    if t.get("__type") == "MtlCookTorrance" and "Diffuse.Opacity" in t.get("Inputs", {}) and "Expression" in t["Inputs"]["Diffuse.Opacity"]:
        fade(n, "Diffuse.Opacity")

# ---- 5. published inputs / output
old_inputs = macro["Inputs"]
keep_order = [
    ("TitleText", "Text"), ("BodyText", "Text"), ("CardScale", "Text"),
    ("ModelShow", "3D Model"), ("ModelKit", "3D Model"), ("ModelScale", "3D Model"),
    ("ModelX", "3D Model"), ("ModelY", "3D Model"), ("ModelZ", "3D Model"),
    ("ModelRotX", "3D Model"), ("ModelRotY", "3D Model"), ("ModelRotZ", "3D Model"),
    ("ModelBright", "3D Model"), ("ModelContrast", "3D Model"), ("ModelSat", "3D Model"),
    ("AnimStart", "Animation"), ("AnimSpeed", "Animation"), ("HoldSeconds", "Animation"), ("Sway", "Animation"),
]
new_inputs = {"__ordered": True}
new_inputs["MainInput1"] = {
    "__type": "InstanceInput", "SourceOp": "FB_EnvSquare", "Source": "Input",
    "Name": "Environment (your plate: the card reflects it)",
}
for key, page in keep_order:
    iv = dict(old_inputs[key])
    iv["Page"] = page
    iv["Name"] = uc[key]["LINKS_Name"]
    new_inputs[key] = iv
new_inputs["EnvOverUnder"] = {
    "__type": "InstanceInput", "SourceOp": "CARD_CONTROLS", "Source": "EnvOverUnder",
    "Name": uc["EnvOverUnder"]["LINKS_Name"], "Page": "Environment", "Default": 1,
}
macro["Inputs"] = new_inputs
macro["Outputs"] = {"MainOutput1": {"__type": "InstanceOutput", "SourceOp": "FB_Scene", "Source": "Output"}}

# ---- 5b. every model file follows the kit folder (task H, 31 Oct: the Salmon .abc was an absolute path on
#          Earth-1610, so the kit control did nothing for it and a tester's Mac could never find the fish)
import os
kit_default = tools["CARD_CONTROLS"]["Inputs"]["ModelKit"]["Value"]
for tname, tool in tools.items():
    for key in ("ImportFile", "Filename"):
        iv = tool.get("Inputs", {}).get(key)
        if isinstance(iv, dict) and isinstance(iv.get("Value"), str) and iv["Value"].startswith(kit_default + "/") and not iv.get("Expression"):
            iv["Expression"] = 'CARD_CONTROLS.ModelKit.Value .. "/%s"' % os.path.basename(iv["Value"])
            print("kit-relative:", tname, key, os.path.basename(iv["Value"]))

# ---- 6. rename and write
newname = "Kalyx3DTitle_" + label.replace(" ", "")
d["Tools"] = {newname: macro}
sw.write_setting(d, dst)

# sanity: every link resolves, every expression's CARD_CONTROLS.X exists
names = set(tools)
bad = [(n, i, so) for n, t in tools.items() for i, so, s in sp.links(t) if so not in names]
import re
refs = set()
for n, t in tools.items():
    for i, iv in t.get("Inputs", {}).items():
        if isinstance(iv, dict) and iv.get("Expression"):
            refs |= set(re.findall(r"CARD_CONTROLS\.([A-Za-z0-9_]+)", iv["Expression"]))
missing_ctl = sorted(r for r in refs if r not in uc)
print(f"{dst}: tools {len(tools)}, inputs {len(new_inputs)-1}, bad links {bad}, controls referenced but missing {missing_ctl}")
