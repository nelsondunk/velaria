#!/usr/bin/env python3
"""The 180 and stereo macros (19 Nov 2026, overnight session):

  Velaria 180 to 360   side-by-side 180 (half-equirect per eye)  ->  over/under 360 (each eye padded to 2:1)
  Velaria 360 to 180   over/under 360                           ->  side-by-side 180 (centre half of each eye)
  Velaria Orient 180   tilt / roll a stereo 180 plate (pan hidden: there is nothing behind the viewer to pan to)
  Velaria Stereo 360   convergence (opposite horizontal shift per eye) + swap eyes, over/under 360, edges wrap
  Velaria Stereo 180   the same for side-by-side 180, edges canvas

Every stock-tool input ID below was read with GetInputList on Resolve 21.1 (18 Nov 2026): Splitter Split/Swap/Input,
Combiner Combine/Swap/Image1/Image2, Crop XOffset/YOffset/XSize/YSize, Transform Center/Edges, Merge Foreground/Background,
Background Width/Height/UseFrameFormatSettings/TopLeftAlpha, PanoMap Rotate.X/Y/Z. Image sizes come from expressions on
other tools' outputs (ToolName.Output.Width), the form Canvas Control already relies on.

Eye order: every chain splits and recombines with Image1 -> Image1 and Image2 -> Image2, so whatever Fusion's left/top
convention is, it is preserved end to end.
"""
import settingwrite as sw

FU = lambda v: {1: v, "__type": "FuID"}   # integer key: settingwrite emits FuID { "Vert" }, a string "1" key emits ["1"] = and the tool ignores it
def I(v=None, **kw):
    d = {"__type": "Input"}
    if v is not None: d["Value"] = v
    d.update(kw)
    return d
def link(op, out="Output"):
    return {"__type": "Input", "SourceOp": op, "Source": out}
def pub(src, source, name, page=None, default=None, lo=None, hi=None):
    d = {"__type": "InstanceInput", "SourceOp": src, "Source": source, "Name": name}
    if page: d["Page"] = page
    if default is not None: d["Default"] = default
    if lo is not None: d["MinScale"] = lo; d["MaxScale"] = hi
    return d
def uc(name, default, lo, hi, page):
    return {"LINKID_DataType": "Number", "INPID_InputControl": "SliderControl", "INP_Default": default,
            "INP_MinScale": lo, "INP_MaxScale": hi, "INP_MinAllowed": lo, "INP_MaxAllowed": hi,
            "LINKS_Name": name, "ICS_ControlPage": page}
def pos(x, y): return {"__type": "OperatorInfo", "Pos": [x, y]}
def macro(name, inputs, outputs, tools, path):
    m = {"__type": "MacroOperator", "Inputs": inputs, "Outputs": outputs, "Tools": tools, "ViewInfo": {"__type": "GroupInfo", "Pos": [0, 0]}}
    sw.write_setting({"Tools": {"__ordered": True, name: m}}, path)
    print("wrote", path)

# ---- building blocks -------------------------------------------------------------------------------------------
def full_domain(name, x=-520, y=0):
    """SetDomain at its defaults (0,0)-(1,1): the whole frame becomes the image's data window. A Renderer3D output
    carries only the card's region as its domain; split and merged again, that region landed at the frame edge
    (chart rig, 18 Nov). Resetting the domain first puts the card where it belongs."""
    return {"__type": "SetDomain", "Inputs": {"Left": I(0), "Bottom": I(0), "Right": I(1), "Top": I(1)}, "ViewInfo": pos(x, y)}
def splitter(name, how, x=-400, y=0):
    # ToolVersion 2 is what Resolve writes for these tools; without it the pair behaves as the old stereo-object
    # tools (Splitter passes the whole frame, Combiner outputs Image1 only) - found 18 Nov on the chart rig
    return {"__type": "Splitter", "CustomData": {"ToolVersion": 2}, "Inputs": {"Split": I(FU(how))}, "ViewInfo": pos(x, y)}
def combiner(name, how, a, b, x=400, y=0):
    return {"__type": "Combiner", "CustomData": {"ToolVersion": 2}, "Inputs": {"Combine": I(FU(how)), "Image1": link(a), "Image2": link(b)}, "ViewInfo": pos(x, y)}
def pad_to_2to1(prefix, split_op, split_out, y):
    """A half-equirect eye (square) centred on a transparent 2:1 canvas twice its width."""
    bg = prefix + "_Canvas"
    mg = prefix + "_Pad"
    return {
        bg: {"__type": "Background", "Inputs": {
            "UseFrameFormatSettings": I(0),
            "Width": I(2048, Expression="%s.%s.Width * 2" % (split_op, split_out)),
            "Height": I(1024, Expression="%s.%s.Height" % (split_op, split_out)),
            "TopLeftAlpha": I(0), "TopLeftRed": I(0), "TopLeftGreen": I(0), "TopLeftBlue": I(0)},
            "ViewInfo": pos(-200, y - 60)},
        mg: {"__type": "Merge", "Inputs": {"Background": link(bg), "Foreground": link(split_op, split_out)}, "ViewInfo": pos(-100, y)},
    }, mg
def centre_half(prefix, src_op, src_out, y, x=200):
    """The middle half (by width) of a 2:1 eye: a square 180 half-equirect. Done as a Merge onto a transparent
    square canvas rather than a Crop: a Crop keeps its offset in the image's data window, and when that image is
    later merged over a plate the card landed at the frame edge (chart rig, 18 Nov)."""
    bg, mg = prefix + "_Square", prefix + "_Centre"
    return {
        bg: {"__type": "Background", "Inputs": {
            "UseFrameFormatSettings": I(0),
            "Width": I(1024, Expression="%s.%s.Width / 2" % (src_op, src_out)),
            "Height": I(1024, Expression="%s.%s.Height" % (src_op, src_out)),
            "TopLeftAlpha": I(0), "TopLeftRed": I(0), "TopLeftGreen": I(0), "TopLeftBlue": I(0)},
            "ViewInfo": pos(x, y - 60)},
        mg: {"__type": "Merge", "Inputs": {"Background": link(bg), "Foreground": link(src_op, src_out)}, "ViewInfo": pos(x + 100, y)},
    }, mg

# ---- Velaria 180 to 360 ------------------------------------------------------------------------------------------
tools = {"__ordered": True}
tools["In180_Full"] = full_domain("In180_Full")
tools["In180_Split"] = splitter("In180_Split", "Horiz"); tools["In180_Split"]["Inputs"]["Input"] = link("In180_Full")
t1, a = pad_to_2to1("Eye1", "In180_Split", "Image1", -80); tools.update(t1)
t2, b = pad_to_2to1("Eye2", "In180_Split", "Image2", 80); tools.update(t2)
tools["Out360_Stack"] = combiner("Out360_Stack", "Vert", a, b)
macro("Velaria_180to360",
      {"__ordered": True, "MainInput1": pub("In180_Full", "Input", "Stereo 180 (Side by Side)")},
      {"MainOutput1": {"__type": "InstanceOutput", "SourceOp": "Out360_Stack", "Source": "Output", "Name": "Stereo 360 (Top / Bottom, padded)"}},
      tools, "Velaria 180 to 360.setting")

# ---- Velaria 360 to 180 ------------------------------------------------------------------------------------------
tools = {"__ordered": True}
tools["In360_Full"] = full_domain("In360_Full")
tools["In360_Split"] = splitter("In360_Split", "Vert"); tools["In360_Split"]["Inputs"]["Input"] = link("In360_Full")
t1, a = centre_half("Eye1", "In360_Split", "Image1", -80, x=0); tools.update(t1)
t2, b = centre_half("Eye2", "In360_Split", "Image2", 80, x=0); tools.update(t2)
tools["Out180_SideBySide"] = combiner("Out180_SideBySide", "Horiz", a, b)
macro("Velaria_360to180",
      {"__ordered": True, "MainInput1": pub("In360_Full", "Input", "Stereo 360 (Top / Bottom)")},
      {"MainOutput1": {"__type": "InstanceOutput", "SourceOp": "Out180_SideBySide", "Source": "Output", "Name": "Stereo 180 (Side by Side)"}},
      tools, "Velaria 360 to 180.setting")

# ---- Velaria Orient 180 ------------------------------------------------------------------------------------------
tools = {"__ordered": True}
tools["Orient_Full"] = full_domain("Orient_Full", x=-720)
tools["Orient_Split"] = splitter("Orient_Split", "Horiz", x=-600); tools["Orient_Split"]["Inputs"]["Input"] = link("Orient_Full")
eyes = []
for n, out, y in (("Eye1", "Image1", -80), ("Eye2", "Image2", 80)):
    tp, padded = pad_to_2to1(n, "Orient_Split", out, y); tools.update(tp)
    pm = n + "_PanoMap"
    rot = {"Rotate.X": I(0), "Rotate.Y": I(0), "Rotate.Z": I(0)} if n == "Eye1" else {
        "Rotate.X": I(0, Expression="Eye1_PanoMap.Rotate.X"), "Rotate.Y": I(0, Expression="Eye1_PanoMap.Rotate.Y"), "Rotate.Z": I(0, Expression="Eye1_PanoMap.Rotate.Z")}
    tools[pm] = {"__type": "PanoMap", "Inputs": dict(Input=link(padded), **rot), "ViewInfo": pos(100, y)}
    tc, cropped = centre_half(n, pm, "Output", y, x=300); tools.update(tc)
    eyes.append(cropped)
tools["Orient_Combine"] = combiner("Orient_Combine", "Horiz", eyes[0], eyes[1], x=600)
macro("Velaria_Orient180",
      {"__ordered": True,
       "MainInput1": pub("Orient_Full", "Input", "Stereo 180 (Side by Side)"),
       "Tilt": pub("Eye1_PanoMap", "Rotate.X", "Tilt", "Placement", 0, -30, 30),
       "Roll": pub("Eye1_PanoMap", "Rotate.Z", "Roll", "Placement", 0, -30, 30)},
      {"MainOutput1": {"__type": "InstanceOutput", "SourceOp": "Orient_Combine", "Source": "Output", "Name": "Stereo 180 (Side by Side)"}},
      tools, "Velaria Orient 180.setting")

# ---- Velaria Stereo 360 / 180 ------------------------------------------------------------------------------------
for label, how, edges, fname in (("360", "Vert", "Wrap", "Velaria Stereo 360.setting"), ("180", "Horiz", "Canvas", "Velaria Stereo 180.setting")):
    tools = {"__ordered": True}
    tools["Velaria_StereoControls"] = {
        "__type": "PipeRouter", "CtrlWZoom": False,
        "Inputs": {"Convergence": I(0)},
        "ViewInfo": {"__type": "PipeRouterInfo", "Pos": [-400, -120]},
        "UserControls": {"__ordered": True, "Convergence": uc("Convergence (+ = push into the screen)", 0, -0.05, 0.05, "Stereo")},
    }
    tools["Stereo_Full"] = full_domain("Stereo_Full")
    tools["Stereo_Split"] = splitter("Stereo_Split", how); tools["Stereo_Split"]["Inputs"]["Input"] = link("Stereo_Full")
    for n, out, y, sign in (("Eye1", "Image1", -80, "+"), ("Eye2", "Image2", 80, "-")):
        # opposite horizontal shifts of half the convergence each: the total disparity change equals the control
        tools[n + "_Shift"] = {"__type": "Transform", "Inputs": {
            "Input": link("Stereo_Split", out),
            "Center": I([0.5, 0.5], Expression="Point(0.5 %s Velaria_StereoControls.Convergence / 2, 0.5)" % sign),
            "Edges": I(FU(edges))}, "ViewInfo": pos(0, y)}
    tools["Stereo_Combine"] = combiner("Stereo_Combine", how, "Eye1_Shift", "Eye2_Shift")
    tools["Stereo_Combine"]["Inputs"]["Swap"] = I(0)
    macro("Velaria_Stereo%s" % label,
          {"__ordered": True,
           "MainInput1": pub("Stereo_Full", "Input", "Stereo %s" % ("360 (Top / Bottom)" if label == "360" else "180 (Side by Side)")),
           "Convergence": pub("Velaria_StereoControls", "Convergence", "Convergence (+ = push into the screen)", "Stereo", 0, -0.05, 0.05),
           "SwapEyes": pub("Stereo_Combine", "Swap", "Swap Eyes", "Stereo", 0, 0, 1)},
          {"MainOutput1": {"__type": "InstanceOutput", "SourceOp": "Stereo_Combine", "Source": "Output", "Name": "Stereo %s" % label}},
          tools, fname)
