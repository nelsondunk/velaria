# Dome Output DCTL check on Bruce, inside "Velaria Format Tests" only: timeline "14 Chart 360 TB dome" (the TB chart,
# 3840 x 3840 @ 30), the DCTL on Color node 1, one PNG frame to ~/VRLink/xfer/rq/fmtout/dome_dctl/.
import json, os, time
assert project.GetName().startswith("Velaria Format Tests"), "wrong project open: " + project.GetName()
HERE = os.path.expanduser("~/VRLink/xfer/rq"); OUT = os.path.join(HERE, "fmtout", "dome_dctl")
os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT): os.unlink(os.path.join(OUT, f))
r = {}
try: r["refresh"] = project.RefreshLUTList()
except Exception as e:
    try: r["refresh"] = resolve.RefreshLUTList()
    except Exception as e2: r["refresh"] = "ERR %s / %s" % (e, e2)
mp = project.GetMediaPool(); root = mp.GetRootFolder()
bins = {f.GetName(): f for f in root.GetSubFolderList()}
charts = bins["Charts"]; clips = {c.GetName(): c for c in (charts.GetClipList() or [])}
c360 = clips["chart_360_tb.png"]
tls = {project.GetTimelineByIndex(i).GetName(): project.GetTimelineByIndex(i) for i in range(1, project.GetTimelineCount() + 1)}
name = "14 Chart 360 TB dome"
t = tls.get(name)
if not t:
    mp.SetCurrentFolder(root)
    t = mp.CreateEmptyTimeline(name); project.SetCurrentTimeline(t)
    for k, v in (("useCustomSettings", "1"), ("timelineFrameRate", "30"), ("timelineResolutionWidth", "3840"), ("timelineResolutionHeight", "3840"),
                 ("timelineOutputResMatchTimelineRes", "1")):
        t.SetSetting(k, v)
    mp.AppendToTimeline([c360])
project.SetCurrentTimeline(t)
it = (t.GetItemListInTrack("video", 1) or [None])[0]
assert it, "no clip"
lut = os.environ.get("LUT", "Velaria/Velaria Dome Output.dctl")
resolve.OpenPage("color"); time.sleep(1.0)
try:
    g = it.GetNodeGraph(1) if hasattr(it, "GetNodeGraph") else None
    r["graph"] = bool(g)
    if g:
        r["nodes"] = g.GetNumNodes()
        r["setlut_graph"] = g.SetLUT(1, lut)
        r["getlut_graph"] = g.GetLUT(1)
except Exception as e: r["graph_err"] = str(e)
if not r.get("setlut_graph"):
    r["setlut_item"] = it.SetLUT(1, lut)
    try: r["getlut_item"] = it.GetLUT(1)
    except Exception as e: r["getlut_item"] = str(e)
for attempt in range(4):
    r["fmtok"] = project.SetCurrentRenderFormatAndCodec("png", "RGB8")
    if r["fmtok"]: break
    resolve.OpenPage("deliver"); time.sleep(1.0)
frame = it.GetStart() + 10
r["setrender"] = project.SetRenderSettings({"SelectAllFrames": False, "MarkIn": frame, "MarkOut": frame, "TargetDir": OUT, "CustomName": "dome_dctl",
                                            "FormatWidth": 3840, "FormatHeight": 3840, "ExportVideo": True, "ExportAudio": False})
job = project.AddRenderJob()
if not job: time.sleep(2); job = project.AddRenderJob()
r["job"] = job
r["start"] = project.StartRendering([job], False) if job else False
t0 = time.time(); time.sleep(1.5)
while project.IsRenderingInProgress():
    time.sleep(0.5)
    if time.time() - t0 > 180: project.StopRendering(); r["timeout"] = True; break
r["status"] = project.GetRenderJobStatus(job) if job else None
try: project.DeleteRenderJob(job)
except Exception: pass
r["files"] = sorted(os.listdir(OUT))
pm.SaveProject()
print(json.dumps(r, default=str))
