"""Builds the Velaria title templates (+94, 21 Nov 2026): Edit-page drop-ons, each a Velaria Canvas Control with a
generic title design wired into it, so a buyer gets a placed, stereo title in one drop and the headset shows it as one
card with Text / Look / Animation tabs beside Placement.

Flat ones (Clean, Lower Third, Caption) draw a 1920 x 1080 graphic with alpha and feed Canvas Control's Graphic input
(Window shows = Graphic). The 3D one (Slab) builds a Text3D over a slab with two lights and feeds the 3D Scene input
(Window shows = Nothing), so it has depth inside the title, not just placement depth.

Animation is by expression on `time` (the pattern the 3D Titles use, proven in Resolve): in from Animation Start over
Animation Length frames, out over the same length before the clip ends when "Animate out" is on.

Usage: python3 make_titles.py            (writes the four .setting files beside this script)
"""
import copy
import os
import sys

import settingparse as sp
import settingwrite as sw

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "Open Sans"   # ships with Resolve
COPPER = (0.80, 0.48, 0.25)
GRAPHITE = (0.08, 0.085, 0.095)

# ---------------------------------------------------------------------------------------------------- helpers

def inp(value=None, expr=None, src=None, out="Output"):
    d = {"__type": "Input"}
    if src is not None:
        d["SourceOp"], d["Source"] = src, out
    elif expr is not None:
        d["Expression"] = expr
    else:
        d["Value"] = value
    return d


def pos(x, y):
    return {"__type": "OperatorInfo", "Pos": [x, y]}


def background(x, y, w=1920, h=1080, rgb=(0, 0, 0), a=0.0, **extra):
    t = {"__type": "Background", "ViewInfo": pos(x, y), "Inputs": {
        "Width": inp(w), "Height": inp(h), "UseFrameFormatSettings": inp(0), "Depth": inp(1),
        "TopLeftRed": inp(rgb[0]), "TopLeftGreen": inp(rgb[1]), "TopLeftBlue": inp(rgb[2]), "TopLeftAlpha": inp(a)}}
    t["Inputs"].update(extra)
    return t


def rect_mask(x, y, center, width, height, radius=0.0):
    def v(q):
        return q if isinstance(q, dict) else inp(q)
    return {"__type": "RectangleMask", "ViewInfo": pos(x, y), "Inputs": {
        "MaskWidth": inp(1920), "MaskHeight": inp(1080), "PixelAspect": inp([1, 1]), "ClippingMode": inp({"__type": "FuID", 1: "None"}),
        "Center": center if isinstance(center, dict) else inp(list(center)), "Width": v(width), "Height": v(height), "CornerRadius": v(radius)}}


def text_plus(x, y, text_expr, size_expr, center, colour_tool, opacity_expr, style="Regular", left=False, spacing=1.0, alpha=1.0):
    t = {"__type": "TextPlus", "ViewInfo": pos(x, y), "Inputs": {
        "Width": inp(1920), "Height": inp(1080), "UseFrameFormatSettings": inp(0), "Depth": inp(1),
        "GlobalOut": inp(20000),
        "StyledText": inp(expr=text_expr), "Font": inp(FONT), "Style": inp(style),
        "Size": inp(expr=size_expr), "CharacterSpacing": inp(spacing),
        "Center": center if isinstance(center, dict) else inp(list(center)),
        "Red1": inp(expr="%s.TopLeftRed" % colour_tool), "Green1": inp(expr="%s.TopLeftGreen" % colour_tool), "Blue1": inp(expr="%s.TopLeftBlue" % colour_tool),
        "Alpha1": inp(alpha), "Opacity1": inp(expr=opacity_expr),
        "VerticalTopCenterBottom": inp(0), "VerticalJustificationNew": inp(3)}}
    if left:
        t["Inputs"]["Alignment"] = inp(1)
        t["Inputs"]["HorizontalLeftCenterRight"] = inp(-1)
        t["Inputs"]["HorizontalJustificationNew"] = inp(3)
    return t


def merge(x, y, bg, fg, blend=None):
    t = {"__type": "Merge", "ViewInfo": pos(x, y), "Inputs": {"Background": inp(src=bg), "Foreground": inp(src=fg), "PerformDepthMerge": inp(0)}}
    if blend is not None:
        t["Inputs"]["Blend"] = blend if isinstance(blend, dict) else inp(blend)
    return t


def slider(name, page, default, lo, hi, integer=False):
    return {"LINKID_DataType": "Number", "INPID_InputControl": "SliderControl", "LINKS_Name": name, "ICS_ControlPage": page,
            "INP_Default": default, "INP_MinScale": lo, "INP_MaxScale": hi, "INP_Integer": integer}


def checkbox(name, page, default):
    return {"LINKID_DataType": "Number", "INPID_InputControl": "CheckboxControl", "LINKS_Name": name, "ICS_ControlPage": page, "INP_Default": default}


def textbox(name, page, default, lines=2):
    return {"LINKID_DataType": "Text", "INPID_InputControl": "TextEditControl", "LINKS_Name": name, "ICS_ControlPage": page,
            "INP_Default": default, "TEC_Lines": lines}


# the in / out curves every template shares, as hidden expression inputs on the controls router
def smooth(u):
    return "((%s)^2 * (3 - 2 * (%s)))" % (u, u)


U_IN = "math.min(1, math.max(0, (time - Title_Controls.AnimStart) / math.max(1, Title_Controls.AnimLen)))"
U_OUT = "math.min(1, math.max(0, (comp:GetAttrs().COMPN_RenderEnd - time) / math.max(1, Title_Controls.AnimLen)))"
IN = smooth(U_IN)
OUT = "iif(Title_Controls.AnimateOut < 0.5, 1, %s)" % smooth(U_OUT)


def controls_router(x, y, fields, extra_user):
    """Title_Controls: the template's own controls (text, sizes, switches) plus hidden In / Out animation values."""
    inputs = {k: inp(v) for k, v in fields.items()}
    inputs["In"] = inp(expr=IN)
    inputs["Out"] = inp(expr=OUT)
    inputs["AnimStart"] = inp(0)
    inputs["AnimLen"] = inp(12)
    inputs["AnimateOut"] = inp(1)
    user = dict(extra_user)
    user["AnimStart"] = slider("Animation start (frame)", "Animation", 0, 0, 120, True)
    user["AnimLen"] = slider("Animation length (frames)", "Animation", 12, 1, 60, True)
    user["AnimateOut"] = checkbox("Animate out before the clip ends", "Animation", 1)
    return {"__type": "PipeRouter", "ViewInfo": {"__type": "PipeRouterInfo", "Pos": [x, y]}, "CtrlWZoom": False, "Inputs": inputs, "UserControls": user}


def colour_inputs(prefix, tool, name, page, group):
    """Three InstanceInputs on one ControlGroup = a colour picker in Resolve and a swatch in the headset."""
    return {
        prefix: {"__type": "InstanceInput", "SourceOp": tool, "Source": "TopLeftRed", "Name": name, "Page": page, "ControlGroup": group, "Default": 1},
        prefix + "G": {"__type": "InstanceInput", "SourceOp": tool, "Source": "TopLeftGreen", "Page": page, "ControlGroup": group, "Default": 1},
        prefix + "B": {"__type": "InstanceInput", "SourceOp": tool, "Source": "TopLeftBlue", "Page": page, "ControlGroup": group, "Default": 1},
    }


def ctrl(key, name, page):
    return {"__type": "InstanceInput", "SourceOp": "Title_Controls", "Source": key, "Name": name, "Page": page}


# ---------------------------------------------------------------------------------------------------- canvas control

def load_canvas():
    rc, _ = sp.load(os.path.join(HERE, "Velaria Canvas Control.setting"))
    ccname, _, _ = sp.tools_of(rc)
    dc = sp.py(rc)
    return dc["Tools"][ccname]


def combo(macro_id, cc, design_tools, design_inputs, graphic_tool=None, scene_tool=None, window_aspect=1.778):
    """Canvas Control + a design, one macro. graphic_tool feeds the Graphic input (Window shows = Graphic);
    scene_tool feeds the 3D Scene input (Window shows = Nothing)."""
    tools = {}
    tools.update(copy.deepcopy(cc["Tools"]))
    clash = set(tools) & set(design_tools)
    assert not clash, clash
    tools.update(design_tools)
    tools["Plate_In"] = {"__type": "PipeRouter", "ViewInfo": {"__type": "PipeRouterInfo", "Pos": [-2300, -395]}}
    tools["Left_Footage_Input"]["Inputs"]["Input"] = inp(src="Plate_In")
    if graphic_tool:
        tools["Graphic_Input"]["Inputs"]["Input"] = inp(src=graphic_tool)
        # the design is 1920 wide; fit it to the window (Canvas Control's own expression assumes a comp-sized graphic)
        tools["Graphic_Input"]["Inputs"]["Size"] = inp(expr="Window_Size_Value.TopLeftRed * Transparent_BG.Width / 1920")
        tools["Velaria_Controls"]["Inputs"]["WindowShows"] = inp(1)
    if scene_tool:
        tools["Scene_Offset"]["Inputs"]["SceneInput"] = inp(src=scene_tool)
        tools["Velaria_Controls"]["Inputs"]["WindowShows"] = inp(2)
    tools["Window_Aspect_Value"]["Inputs"]["TopLeftRed"] = inp(window_aspect)
    tools["Card_Over_Plate"] = merge(1200, -395, "Plate_In", "Stack_Eyes_OverUnder")

    inputs = {"__ordered": True}
    inputs["MainInput1"] = {"__type": "InstanceInput", "SourceOp": "Plate_In", "Source": "Input", "Name": "Plate (360 footage)"}
    cci = cc["Inputs"]
    for key in ("Yaw", "Pitch", "Roll", "Distance", "ObjectTurn", "ObjectTilt", "ObjectSpin"):
        inputs[key] = copy.deepcopy(cci[key])
    inputs.update(design_inputs)
    for key in ("WindowSize", "GlowAmount", "GlowSize", "GlowBrightness") if graphic_tool else ():
        inputs[key] = copy.deepcopy(cci[key])
        inputs[key]["Page"] = "Look"
    if graphic_tool:
        inputs["WindowSize"]["Name"] = "Size in the window"
    for key in ("EyeSeparation", "WindowDepth", "OutputWidth"):
        inputs[key] = copy.deepcopy(cci[key])
    outputs = {"MainOutput1": {"__type": "InstanceOutput", "SourceOp": "Card_Over_Plate", "Source": "Output", "Name": "Over plate (Over/Under Stereo)"}}
    macro = {"__type": "MacroOperator", "CtrlWZoom": False, "Inputs": inputs, "Outputs": outputs, "Tools": {"__ordered": True, **tools},
             "ViewInfo": {"__type": "GroupInfo", "Pos": [0, 0]}}
    return {"__type": None, "Tools": {"__ordered": True, macro_id: macro}}


# ---------------------------------------------------------------------------------------------------- designs

def design_clean():
    """A title and a line under it, left-aligned, a copper rule between: the plain one."""
    t = {}
    t["Title_Controls"] = controls_router(-900, -200, {"TitleText": "Title", "SubText": "A line about it", "Size": 1.0, "Rise": 1}, {
        "TitleText": textbox("Title", "Text", "Title", 2),
        "SubText": textbox("Line under it", "Text", "A line about it", 2),
        "Size": slider("Text size", "Text", 1.0, 0.5, 2.0),
        "Rise": checkbox("Rise in", "Animation", 1),
    })
    t["Text_Colour"] = background(-900, -100, 4, 4, (1, 1, 1), 1)
    t["Accent_Colour"] = background(-900, -50, 4, 4, COPPER, 1)
    t["Title_BG"] = background(-800, 0)
    rise = "iif(Title_Controls.Rise > 0.5, 0.05 * (1 - Title_Controls.In), 0)"
    t["Title_Text"] = text_plus(-700, 0, "Text(Title_Controls.TitleText.Value)", "0.105 * Title_Controls.Size",
                                inp(expr="Point(0.12, 0.555 - %s)" % rise), "Text_Colour", "Title_Controls.In * Title_Controls.Out", style="Bold", left=True, spacing=0.98)
    t["Sub_Text"] = text_plus(-700, 60, "Text(Title_Controls.SubText.Value)", "0.046 * Title_Controls.Size",
                              inp(expr="Point(0.122, 0.448 - %s)" % rise), "Text_Colour", "Title_Controls.In * Title_Controls.Out", alpha=0.82, left=True)
    t["Rule_Mask"] = rect_mask(-700, 120, inp(expr="Point(0.12 + 0.11 * Title_Controls.Size * Title_Controls.In, 0.503 - %s)" % rise),
                               inp(expr="0.22 * Title_Controls.Size * Title_Controls.In"), inp(expr="0.006 * Title_Controls.Size"))
    t["Rule"] = background(-600, 120, rgb=(0, 0, 0), a=1.0, EffectMask=inp(src="Rule_Mask", out="Mask"))
    for ch, c in zip(("Red", "Green", "Blue"), COPPER):
        t["Rule"]["Inputs"]["TopLeft" + ch] = inp(expr="Accent_Colour.TopLeft" + ch)
    t["Rule"]["Inputs"]["TopLeftAlpha"] = inp(expr="Title_Controls.Out")
    t["M1"] = merge(-500, 0, "Title_BG", "Rule")
    t["M2"] = merge(-400, 0, "M1", "Title_Text")
    t["Title_Out"] = merge(-300, 0, "M2", "Sub_Text")
    inputs = {"__ordered": True}
    inputs["TitleText"] = ctrl("TitleText", "Title", "Text")
    inputs["SubText"] = ctrl("SubText", "Line under it", "Text")
    inputs["Size"] = ctrl("Size", "Text size", "Text")
    inputs.update(colour_inputs("TextColour", "Text_Colour", "Text colour", "Look", 21))
    inputs.update(colour_inputs("AccentColour", "Accent_Colour", "Rule colour", "Look", 22))
    inputs["AnimStart"] = ctrl("AnimStart", "Animation start (frame)", "Animation")
    inputs["AnimLen"] = ctrl("AnimLen", "Animation length (frames)", "Animation")
    inputs["Rise"] = ctrl("Rise", "Rise in", "Animation")
    inputs["AnimateOut"] = ctrl("AnimateOut", "Animate out before the clip ends", "Animation")
    return t, inputs, "Title_Out"


def design_lower_third():
    """Name and role on a translucent plate with a copper edge, bottom left; the plate wipes in from the left."""
    t = {}
    t["Title_Controls"] = controls_router(-900, -200, {"TitleText": "Name", "SubText": "Role or place", "Size": 1.0, "PlateOpacity": 0.72, "PlateWidth": 0.50}, {
        "TitleText": textbox("Name", "Text", "Name", 1),
        "SubText": textbox("Role or place", "Text", "Role or place", 1),
        "Size": slider("Text size", "Text", 1.0, 0.5, 2.0),
        "PlateOpacity": slider("Plate opacity", "Look", 0.72, 0.0, 1.0),
        "PlateWidth": slider("Plate width", "Look", 0.50, 0.2, 0.9),
    })
    t["Text_Colour"] = background(-900, -100, 4, 4, (1, 1, 1), 1)
    t["Accent_Colour"] = background(-900, -50, 4, 4, COPPER, 1)
    t["Plate_Colour"] = background(-900, 0, 4, 4, GRAPHITE, 1)
    t["Title_BG"] = background(-800, 0)
    w = "Title_Controls.PlateWidth * Title_Controls.In"
    t["Plate_Mask"] = rect_mask(-700, 100, inp(expr="Point(0.06 + 0.5 * %s, 0.20)" % w), inp(expr=w), inp(expr="0.15 * Title_Controls.Size"), 0.012)
    t["Plate"] = background(-600, 100, a=1.0, EffectMask=inp(src="Plate_Mask", out="Mask"))
    for ch in ("Red", "Green", "Blue"):
        t["Plate"]["Inputs"]["TopLeft" + ch] = inp(expr="Plate_Colour.TopLeft" + ch)
    t["Plate"]["Inputs"]["TopLeftAlpha"] = inp(expr="Title_Controls.PlateOpacity * Title_Controls.Out")
    t["Edge_Mask"] = rect_mask(-700, 160, inp(expr="Point(0.064, 0.20)"), 0.008, inp(expr="0.15 * Title_Controls.Size"))
    t["Edge"] = background(-600, 160, a=1.0, EffectMask=inp(src="Edge_Mask", out="Mask"))
    for ch in ("Red", "Green", "Blue"):
        t["Edge"]["Inputs"]["TopLeft" + ch] = inp(expr="Accent_Colour.TopLeft" + ch)
    t["Edge"]["Inputs"]["TopLeftAlpha"] = inp(expr="Title_Controls.In * Title_Controls.Out")
    fade = "math.max(0, Title_Controls.In * 1.6 - 0.6) * Title_Controls.Out"   # the words follow the plate
    t["Title_Text"] = text_plus(-700, 0, "Text(Title_Controls.TitleText.Value)", "0.052 * Title_Controls.Size",
                                inp(expr="Point(0.095, 0.20 + 0.03 * Title_Controls.Size)"), "Text_Colour", fade, style="Bold", left=True)
    t["Sub_Text"] = text_plus(-700, 60, "Text(Title_Controls.SubText.Value)", "0.034 * Title_Controls.Size",
                              inp(expr="Point(0.096, 0.20 - 0.032 * Title_Controls.Size)"), "Text_Colour", fade, alpha=0.85, left=True)
    t["M1"] = merge(-500, 0, "Title_BG", "Plate")
    t["M2"] = merge(-450, 0, "M1", "Edge")
    t["M3"] = merge(-400, 0, "M2", "Title_Text")
    t["Title_Out"] = merge(-300, 0, "M3", "Sub_Text")
    inputs = {"__ordered": True}
    inputs["TitleText"] = ctrl("TitleText", "Name", "Text")
    inputs["SubText"] = ctrl("SubText", "Role or place", "Text")
    inputs["Size"] = ctrl("Size", "Text size", "Text")
    inputs.update(colour_inputs("TextColour", "Text_Colour", "Text colour", "Look", 21))
    inputs.update(colour_inputs("PlateColour", "Plate_Colour", "Plate colour", "Look", 22))
    inputs["PlateOpacity"] = ctrl("PlateOpacity", "Plate opacity", "Look")
    inputs["PlateWidth"] = ctrl("PlateWidth", "Plate width", "Look")
    inputs.update(colour_inputs("AccentColour", "Accent_Colour", "Edge colour", "Look", 23))
    inputs["AnimStart"] = ctrl("AnimStart", "Animation start (frame)", "Animation")
    inputs["AnimLen"] = ctrl("AnimLen", "Animation length (frames)", "Animation")
    inputs["AnimateOut"] = ctrl("AnimateOut", "Animate out before the clip ends", "Animation")
    return t, inputs, "Title_Out"


def design_caption():
    """One line on a dark pill, centred low: subtitles, place names, a credit."""
    t = {}
    t["Title_Controls"] = controls_router(-900, -200, {"TitleText": "A caption", "Size": 1.0, "PillOpacity": 0.6, "PillWidth": 0.5}, {
        "TitleText": textbox("Caption", "Text", "A caption", 1),
        "Size": slider("Text size", "Text", 1.0, 0.5, 2.0),
        "PillOpacity": slider("Pill opacity", "Look", 0.6, 0.0, 1.0),
        "PillWidth": slider("Pill width", "Look", 0.5, 0.15, 0.95),
    })
    t["Text_Colour"] = background(-900, -100, 4, 4, (1, 1, 1), 1)
    t["Plate_Colour"] = background(-900, 0, 4, 4, GRAPHITE, 1)
    t["Title_BG"] = background(-800, 0)
    t["Pill_Mask"] = rect_mask(-700, 100, inp(expr="Point(0.5, 0.14)"), inp(expr="Title_Controls.PillWidth * Title_Controls.In"),
                               inp(expr="0.085 * Title_Controls.Size"), 1.0)
    t["Pill"] = background(-600, 100, a=1.0, EffectMask=inp(src="Pill_Mask", out="Mask"))
    for ch in ("Red", "Green", "Blue"):
        t["Pill"]["Inputs"]["TopLeft" + ch] = inp(expr="Plate_Colour.TopLeft" + ch)
    t["Pill"]["Inputs"]["TopLeftAlpha"] = inp(expr="Title_Controls.PillOpacity * Title_Controls.Out")
    t["Title_Text"] = text_plus(-700, 0, "Text(Title_Controls.TitleText.Value)", "0.042 * Title_Controls.Size",
                                inp(expr="Point(0.5, 0.14)"), "Text_Colour", "Title_Controls.In * Title_Controls.Out")
    t["M1"] = merge(-500, 0, "Title_BG", "Pill")
    t["Title_Out"] = merge(-400, 0, "M1", "Title_Text")
    inputs = {"__ordered": True}
    inputs["TitleText"] = ctrl("TitleText", "Caption", "Text")
    inputs["Size"] = ctrl("Size", "Text size", "Text")
    inputs.update(colour_inputs("TextColour", "Text_Colour", "Text colour", "Look", 21))
    inputs.update(colour_inputs("PlateColour", "Plate_Colour", "Pill colour", "Look", 22))
    inputs["PillOpacity"] = ctrl("PillOpacity", "Pill opacity", "Look")
    inputs["PillWidth"] = ctrl("PillWidth", "Pill width", "Look")
    inputs["AnimStart"] = ctrl("AnimStart", "Animation start (frame)", "Animation")
    inputs["AnimLen"] = ctrl("AnimLen", "Animation length (frames)", "Animation")
    inputs["AnimateOut"] = ctrl("AnimateOut", "Animate out before the clip ends", "Animation")
    return t, inputs, "Title_Out"


def design_slab():
    """Text3D over a slab, lit: depth inside the title. The slab scales in from nothing, the text fades."""
    t = {}
    t["Title_Controls"] = controls_router(-900, -200, {"TitleText": "Title", "Size": 1.0, "Depth": 0.12, "SlabOn": 1, "SlabWidth": 2.6, "SlabHeight": 0.8}, {
        "TitleText": textbox("Title", "Text", "Title", 2),
        "Size": slider("Text size", "Text", 1.0, 0.4, 2.5),
        "Depth": slider("Letter depth", "Look", 0.12, 0.0, 0.5),
        "SlabOn": checkbox("Slab behind the text", "Look", 1),
        "SlabWidth": slider("Slab width", "Look", 2.6, 0.5, 6.0),
        "SlabHeight": slider("Slab height", "Look", 0.8, 0.2, 3.0),
    })
    t["Text_Colour"] = background(-900, -100, 4, 4, (0.96, 0.93, 0.88), 1)
    t["Plate_Colour"] = background(-900, 0, 4, 4, (0.13, 0.13, 0.15), 1)
    t["Slab_Text"] = {"__type": "Text3D", "ViewInfo": pos(-700, 0), "Inputs": {
        "StyledText": inp(expr="Text(Title_Controls.TitleText.Value)"), "Font": inp(FONT), "Style": inp("Bold"),
        "Size": inp(expr="0.42 * Title_Controls.Size"), "CharacterSpacing": inp(0.98), "LineSpacing": inp(0.9),
        "Extrusion": inp(1), "ExtrusionDepth": inp(expr="Title_Controls.Depth"), "UseOneMaterial": inp(1),
        "Red1": inp(expr="Text_Colour.TopLeftRed"), "Green1": inp(expr="Text_Colour.TopLeftGreen"), "Blue1": inp(expr="Text_Colour.TopLeftBlue"),
        "Opacity1": inp(expr="Title_Controls.In * Title_Controls.Out"),
        "VerticalTopCenterBottom": inp(0), "VerticalJustificationNew": inp(3),
        "Transform3DOp.Translate.Z": inp(0.02)}}
    t["Slab_Plate"] = {"__type": "Shape3D", "ViewInfo": pos(-700, 100), "Inputs": {
        "Shape": inp({"__type": "FuID", 1: "SurfaceCubeInputs"}),
        "SurfaceCubeInputs.SizeLock": inp(0),
        "SurfaceCubeInputs.Width": inp(expr="Title_Controls.SlabWidth * Title_Controls.Size"),
        "SurfaceCubeInputs.Height": inp(expr="Title_Controls.SlabHeight * Title_Controls.Size"),
        "SurfaceCubeInputs.Depth": inp(0.05),
        "SurfaceCubeInputs.ObjectID.ObjectID": inp(2), "MtlStdInputs.MaterialID": inp(1),
        "MtlStdInputs.Diffuse.Color.Red": inp(expr="Plate_Colour.TopLeftRed"), "MtlStdInputs.Diffuse.Color.Green": inp(expr="Plate_Colour.TopLeftGreen"),
        "MtlStdInputs.Diffuse.Color.Blue": inp(expr="Plate_Colour.TopLeftBlue"),
        "MtlStdInputs.Diffuse.Opacity": inp(expr="Title_Controls.Out"),
        "Transform3DOp.ScaleLock": inp(0),
        "Transform3DOp.Scale.X": inp(expr="iif(Title_Controls.SlabOn < 0.5, 0.001, math.max(0.001, Title_Controls.In))"),
        "Transform3DOp.Scale.Y": inp(expr="iif(Title_Controls.SlabOn < 0.5, 0.001, 1)"),
        "Transform3DOp.Translate.Z": inp(-0.06)}}
    t["Slab_Key"] = {"__type": "LightDirectional", "ViewInfo": pos(-700, 200), "Inputs": {
        "Intensity": inp(0.9), "Transform3DOp.Rotate.X": inp(-28), "Transform3DOp.Rotate.Y": inp(32)}}
    t["Slab_Fill"] = {"__type": "LightAmbient", "ViewInfo": pos(-700, 260), "Inputs": {"Intensity": inp(0.35)}}
    t["Slab_Scene"] = {"__type": "Merge3D", "ViewInfo": pos(-500, 100), "Inputs": {
        "SceneInput1": inp(src="Slab_Plate"), "SceneInput2": inp(src="Slab_Text"), "SceneInput3": inp(src="Slab_Key"), "SceneInput4": inp(src="Slab_Fill")}}
    inputs = {"__ordered": True}
    inputs["TitleText"] = ctrl("TitleText", "Title", "Text")
    inputs["Size"] = ctrl("Size", "Text size", "Text")
    inputs.update(colour_inputs("TextColour", "Text_Colour", "Text colour", "Look", 21))
    inputs["Depth"] = ctrl("Depth", "Letter depth", "Look")
    inputs["SlabOn"] = ctrl("SlabOn", "Slab behind the text", "Look")
    inputs.update(colour_inputs("PlateColour", "Plate_Colour", "Slab colour", "Look", 22))
    inputs["SlabWidth"] = ctrl("SlabWidth", "Slab width", "Look")
    inputs["SlabHeight"] = ctrl("SlabHeight", "Slab height", "Look")
    inputs["AnimStart"] = ctrl("AnimStart", "Animation start (frame)", "Animation")
    inputs["AnimLen"] = ctrl("AnimLen", "Animation length (frames)", "Animation")
    inputs["AnimateOut"] = ctrl("AnimateOut", "Animate out before the clip ends", "Animation")
    return t, inputs, "Slab_Scene"


TEMPLATES = [
    ("Velaria Title - Clean.setting", "Velaria_TitleClean", design_clean, "graphic"),
    ("Velaria Lower Third.setting", "Velaria_LowerThird", design_lower_third, "graphic"),
    ("Velaria Caption.setting", "Velaria_Caption", design_caption, "graphic"),
    ("Velaria Title 3D - Slab.setting", "Velaria_TitleSlab", design_slab, "scene"),
]


def main():
    cc = load_canvas()
    out_dir = sys.argv[1] if len(sys.argv) > 1 else HERE
    for fname, macro_id, design, kind in TEMPLATES:
        tools, inputs, out_tool = design()
        root = combo(macro_id, cc, tools, inputs, graphic_tool=out_tool if kind == "graphic" else None, scene_tool=out_tool if kind == "scene" else None)
        path = os.path.join(out_dir, fname)
        sw.write_setting(root, path)
        print("wrote", path)


if __name__ == "__main__":
    main()
