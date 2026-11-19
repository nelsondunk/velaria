#!/usr/bin/env python3
"""Why does Canvas Control's output land wrong when Merged over the plate inside one comp? Six comps on the 360 chart
timeline (13 Chart 360 TB), rendered by s_fmtrig.py with VARIANTS from this file's variants.json (OUT dir)."""
import copy, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import settingparse as sp, settingwrite as sw

OUT = sys.argv[1] if len(sys.argv) > 1 else "fmtdiag"
os.makedirs(OUT, exist_ok=True)
RANGE = 59
MAC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Velaria Canvas Control.setting")
def I(v, **kw):
    d = {"__type": "Input", "Value": v}; d.update(kw); return d
def link(op, out="Output"): return {"__type": "Input", "SourceOp": op, "Source": out}
def pos(x, y): return {"__type": "OperatorInfo", "Pos": [x, y]}
def macro(path):
    r, _ = sp.load(path); name, t, _ = sp.tools_of(r)
    return name, copy.deepcopy(sp.py(r)["Tools"][name])
def set_inner(m, instance_input, value):
    ii = m["Inputs"][instance_input]; tool = m["Tools"][ii["SourceOp"]]; tool.setdefault("Inputs", {})
    cur = tool["Inputs"].get(ii["Source"]); cur = dict(cur) if isinstance(cur, dict) else {"__type": "Input"}
    cur.pop("Expression", None); cur["Value"] = value; cur["__type"] = "Input"; tool["Inputs"][ii["Source"]] = cur; ii["Default"] = value
def connect_inner(m, instance_input, src_op, src_out):
    ii = m["Inputs"][instance_input]; m["Tools"][ii["SourceOp"]].setdefault("Inputs", {})[ii["Source"]] = link(src_op, src_out)
def out_of(m, o="MainOutput1"): return m["Outputs"][o]["SourceOp"], m["Outputs"][o]["Source"]
def bgtool(w, h, r, g, b, a=1, x=0, y=0):
    return {"__type": "Background", "Inputs": {"UseFrameFormatSettings": I(0), "Width": I(w), "Height": I(h),
            "TopLeftRed": I(r), "TopLeftGreen": I(g), "TopLeftBlue": I(b), "TopLeftAlpha": I(a)}, "ViewInfo": pos(x, y)}
def merge(bg_op, bg_out, fg_op, fg_out, x=600, y=0, **extra):
    inp = {"Background": link(bg_op, bg_out), "Foreground": link(fg_op, fg_out)}
    for k, v in extra.items(): inp[k] = I(v)
    return {"__type": "Merge", "Inputs": inp, "ViewInfo": pos(x, y)}
def comp(tools_ordered, out_src, out_name, w, h):
    tools = {"__ordered": True}
    tools["MediaIn1"] = {"__type": "MediaIn", "Inputs": {"Layer": I("0"), "GlobalIn": I(0), "GlobalOut": I(RANGE)}, "ViewInfo": pos(-700, 0)}
    for k, v in tools_ordered: tools[k] = v
    tools["MediaOut1"] = {"__type": "MediaOut", "Inputs": {"Index": I("0"), "Input": link(out_src, out_name)}, "ViewInfo": pos(800, 0)}
    return {"__type": "Composition", "CurrentTime": 0, "RenderRange": [0, RANGE], "GlobalRange": [0, RANGE], "HiQ": True, "PlaybackUpdateMode": 0,
            "Version": "DaVinci Resolve Studio 21.1.0.0017", "SavedOutputs": 1, "Resumable": True, "Tools": tools, "Frames": {},
            "Prefs": {"Comp": {"FrameFormat": {"Width": w, "Height": h, "Rate": 30, "GuideRatio": w / h, "DepthFull": 3, "DepthPreview": 3, "DepthInteractive": 3},
                               "Unsorted": {"GlobalEnd": RANGE}}}}
variants = []
def emit(vid, c, desc, expect):
    p = os.path.join(OUT, vid + ".comp"); sw.write_setting(c, p)
    variants.append({"id": vid, "timeline": "13 Chart 360 TB", "file": os.path.basename(p), "desc": desc, "expect": expect})
W = H = 3840
def cc(src_op="MediaIn1", src_out="Output"):
    n, m = macro(MAC); connect_inner(m, "MainInput1", src_op, src_out); set_inner(m, "Mode", 1); set_inner(m, "OutputWidth", 3840); return n, m

# A. Merge baseline: a 400x400 red square over the chart
emit("mg_fg_bgtool", comp([("Red", bgtool(400, 400, 1, 0, 0)), ("M", merge("MediaIn1", "Output", "Red", "Output"))], "M", "Output", W, H),
     "Merge(chart, 400x400 red Background)", "red square at the centre of the frame (Merge centres a smaller FG)")
# F. CC straight out on the 360 timeline (reference)
n, m = cc(); emit("mg_cc_alone360", comp([(n, m)], *out_of(m), W, H), "CC stereo -> MediaOut", "card centred in both eyes")
# B. Merge over chart, FG = CC mono output
n, m = cc(); emit("mg_cc_main2", comp([(n, m), ("M", merge("MediaIn1", "Output", *out_of(m, "MainOutput2")))], "M", "Output", W, H),
     "Merge(chart, CC.MainOutput2)", "one card, centred in the top (L) eye or the frame centre")
# C. CC fed by a Background, not by MediaIn1 (breaks the diamond)
n, m = cc("Green", "Output")
emit("mg_cc_bgin", comp([("Green", bgtool(1920, 1080, 0, 1, 0)), (n, m), ("M", merge("MediaIn1", "Output", *out_of(m)))], "M", "Output", W, H),
     "CC window = green Background; Merge over chart", "green card centred in both eyes")
# D. SetDomain full after CC, then Merge
n, m = cc()
sd = {"__type": "SetDomain", "Inputs": {"Left": I(0), "Bottom": I(0), "Right": I(1), "Top": I(1), "Input": link(*out_of(m))}, "ViewInfo": pos(400, 0)}
emit("mg_cc_setdom", comp([(n, m), ("SD", sd), ("M", merge("MediaIn1", "Output", "SD", "Output"))], "M", "Output", W, H),
     "CC -> SetDomain(full) -> Merge over chart", "card centred in both eyes")
# E. Merge over a grey Background instead of MediaIn1
n, m = cc()
emit("mg_cc_greybg", comp([("Grey", bgtool(W, H, 0.3, 0.3, 0.3)), (n, m), ("M", merge("Grey", "Output", *out_of(m)))], "M", "Output", W, H),
     "Merge(grey 3840x3840, CC)", "card centred in both eyes on grey")
# G. Merge with the chart as FG at 50% over CC as BG
n, m = cc()
emit("mg_cc_asbg", comp([(n, m), ("M", merge(out_of(m)[0], out_of(m)[1], "MediaIn1", "Output", Blend=0.5))], "M", "Output", W, H),
     "Merge(CC as BG, chart as FG at 50%)", "card centred in both eyes under a half-transparent chart")
json.dump(variants, open(os.path.join(OUT, "variants.json"), "w"), indent=1)
print(len(variants), "comps in", OUT)

# --- size probe by cropping (added 21:05): a Crop at a fixed 3840x3840 window shows what the stack really is
if __name__ == "__main__" and os.environ.get("SIZEPROBE") == "1":
    variants.clear()
    for vid, yoff, desc in (("sz_crop_bottom", 0, "Crop 3840x3840 at (0,0) of the stack"), ("sz_crop_top", 3840, "Crop 3840x3840 at (0,3840) of the stack")):
        n, m = cc()
        crop = {"__type": "Crop", "Inputs": {"XOffset": I(0), "YOffset": I(yoff), "XSize": I(3840), "YSize": I(3840), "Input": link(*out_of(m))}, "ViewInfo": pos(400, 0)}
        emit(vid, comp([(n, m), ("Cr", crop)], "Cr", "Output", W, H), desc,
             "if the stack is 3840x3840: bottom = whole stack, top = empty; if 3840x7680: bottom = one eye stretched tall, top = the other")
    json.dump(variants, open(os.path.join(OUT, "variants.json"), "w"), indent=1)
    print(len(variants), "size-probe comps in", OUT)
