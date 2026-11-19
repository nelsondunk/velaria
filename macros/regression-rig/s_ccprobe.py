import json
assert project.GetName().startswith("Velaria Format Tests")
tls = {project.GetTimelineByIndex(i).GetName(): project.GetTimelineByIndex(i) for i in range(1, project.GetTimelineCount() + 1)}
t = tls["13 Chart 360 TB"]; project.SetCurrentTimeline(t)
it = (t.GetItemListInTrack("video", 1) or [None])[0]
names = it.GetFusionCompNameList(); r = {"comps": names}
c = it.GetFusionCompByName(names[-1])
toolmap = {x.Name: x for x in (c.GetToolList() or {}).values()}
r["tools"] = sorted(toolmap)
def probe(tool, outname):
    out = None
    try:
        o = tool.GetOutputList()
        for k, v in (o or {}).items():
            if v.Name == outname or v.ID == outname: out = v
    except Exception as e: return "ERR list %s" % e
    if out is None: return "no output " + outname
    try:
        img = out.GetValue()
    except Exception as e: return "ERR getvalue %s" % e
    if img is None: return "None"
    d = {}
    for a in ("Width", "Height", "XScale", "YScale", "Depth", "ImageAspect"):
        try: d[a] = getattr(img, a)
        except Exception as e: d[a] = "ERR"
    try:
        dw = img.DataWindow; d["DataWindow"] = [dw[1], dw[2], dw[3], dw[4]] if dw else None
    except Exception as e: d["DataWindow"] = "ERR %s" % e
    try:
        at = img.GetAttrs(); d["attrs"] = {k: str(v) for k, v in at.items() if k in ("IMGI_Width","IMGI_Height","IMGD_XScale","IMGD_YScale","IMGI_DataWindow")}
    except Exception as e: d["attrs"] = "ERR %s" % str(e)[:60]
    return d
for tn in r["tools"]:
    tool = toolmap[tn]
    if tn.startswith("Velaria_CanvasControl"):
        r[tn + ".MainOutput1"] = probe(tool, "MainOutput1"); r[tn + ".MainOutput2"] = probe(tool, "MainOutput2")
        # inner renderer sizes
        try:
            for inner in ("Render_LeftEye", "Render_RightEye", "Stack_Eyes_OverUnder"):
                subs = {x.Name: x for x in (tool.GetToolList() or {}).values()} if hasattr(tool, "GetToolList") else {}
                r[tn + "/" + inner] = probe(subs[inner], "Output") if inner in subs else "not found (%d inner)" % len(subs)
        except Exception as e: r["inner_err"] = str(e)
    elif tn in ("MediaIn1", "M", "Grey", "Red", "SD"):
        r[tn] = probe(tool, "Output")
print(json.dumps(r, default=str)[:3000])
