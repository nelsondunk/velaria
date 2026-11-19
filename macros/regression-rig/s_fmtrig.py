# 180 / stereo macro checks on Bruce, inside the "Velaria Format Tests" project only. Env: VARIANTS=all|comma list,
# DRY=1 (import comps, no render). Charts come from ~/VRLink/xfer/rq/charts, comps from ~/VRLink/xfer/rq/fmt180,
# renders go to ~/VRLink/xfer/rq/fmtout/<id>/<id>_<frame>.png at the timeline's own size.
import json, os, time
assert project.GetName().startswith("Velaria Format Tests"), "wrong project open: " + project.GetName()
HERE = os.path.expanduser("~/VRLink/xfer/rq")
COMPS, CHARTS, OUTDIR = os.environ.get("COMPS", os.path.join(HERE, "fmt180")), os.path.join(HERE, "charts"), os.path.join(HERE, "fmtout")
LOG = os.path.expanduser("~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/davinci_resolve.log")
RESULT = os.path.join(HERE, "fmtresult.json")
os.makedirs(OUTDIR, exist_ok=True)
want = os.environ.get("VARIANTS", "all"); dry = os.environ.get("DRY") == "1"
variants = json.load(open(os.path.join(COMPS, "variants.json")))
if want != "all":
    keep = set(want.split(",")); variants = [v for v in variants if v["id"] in keep]

mp = project.GetMediaPool(); root = mp.GetRootFolder()
# charts bin + clips
bins = {f.GetName(): f for f in root.GetSubFolderList()}
charts = bins.get("Charts") or mp.AddSubFolder(root, "Charts")
mp.SetCurrentFolder(charts)
have = {c.GetName(): c for c in (charts.GetClipList() or [])}
def chart(fname):
    if fname in have: return have[fname]
    items = mp.ImportMedia([os.path.join(CHARTS, fname)])
    return items[0] if items else None
c180, c360 = chart("chart_180_sbs.png"), chart("chart_360_tb.png")
mp.SetCurrentFolder(root)
# chart timelines (a still clip is 5 s on the timeline by default; the comp's range covers it)
tls = {project.GetTimelineByIndex(i).GetName(): project.GetTimelineByIndex(i) for i in range(1, project.GetTimelineCount() + 1)}
def chart_tl(name, clip, w, h):
    if name in tls: return tls[name]
    t = mp.CreateEmptyTimeline(name); project.SetCurrentTimeline(t)
    for k, v in (("useCustomSettings", "1"), ("timelineFrameRate", "30"), ("timelineResolutionWidth", str(w)), ("timelineResolutionHeight", str(h)),
                 ("timelineOutputResMatchTimelineRes", "1")):
        t.SetSetting(k, v)
    mp.AppendToTimeline([clip]); tls[name] = t
    return t
chart_tl("12 Chart 180 SBS", c180, 3840, 1920)
chart_tl("13 Chart 360 TB", c360, 3840, 3840)

results = {}
try: results = json.load(open(RESULT))
except Exception: pass
def log_pos():
    try: return os.path.getsize(LOG)
    except Exception: return 0
def log_tail(p):
    try:
        with open(LOG, "rb") as f: f.seek(p); return f.read().decode("utf-8", "replace")
    except Exception as e: return "LOGERR %s" % e

for v in variants:
    vid = v["id"]; t0 = time.time(); pos = log_pos()
    t = tls[v["timeline"]]; project.SetCurrentTimeline(t)
    it = (t.GetItemListInTrack("video", 1) or [None])[0]
    if it is None:
        results[vid] = {"status": "FAIL", "why": "no clip on " + v["timeline"]}; continue
    for old in list(it.GetFusionCompNameList() or []):   # one comp per check, never stacked
        try: it.DeleteFusionCompByName(old)
        except Exception as e: print("cleanup", vid, e)
    comp = it.ImportFusionComp(os.path.join(COMPS, v["file"]))
    if comp:
        names = it.GetFusionCompNameList() or []
        if names: it.LoadFusionCompByName(names[-1])
    tools = sorted(x.Name for x in (comp.GetToolList() or {}).values()) if comp else []
    r = {"desc": v["desc"], "expect": v["expect"], "timeline": v["timeline"], "import": bool(comp), "ntools": len(tools), "tools": tools[:14]}
    if dry or not comp:
        r["status"] = "IMPORTED" if comp else "FAIL"; results[vid] = r
        json.dump(results, open(RESULT, "w"), indent=1); print(vid, r["status"], len(tools), "tools"); continue
    vdir = os.path.join(OUTDIR, vid); os.makedirs(vdir, exist_ok=True)
    for f in os.listdir(vdir): os.unlink(os.path.join(vdir, f))
    # right after a comp import the format call can fail once (page still settling); retry on the Deliver page
    for attempt in range(4):
        r["fmtok"] = project.SetCurrentRenderFormatAndCodec("png", "RGB8")   # ids from GetRenderCodecs("png")
        if r["fmtok"]: break
        resolve.OpenPage("deliver"); time.sleep(1.0)
    r["fmt"] = project.GetCurrentRenderFormatAndCodec()
    w, h = int(t.GetSetting("timelineResolutionWidth")), int(t.GetSetting("timelineResolutionHeight"))
    frame = it.GetStart() + 10
    r["setrender"] = project.SetRenderSettings({"SelectAllFrames": False, "MarkIn": frame, "MarkOut": frame, "TargetDir": vdir, "CustomName": vid,
                                                "FormatWidth": w, "FormatHeight": h, "ExportVideo": True, "ExportAudio": False})
    job = project.AddRenderJob(); r["job"] = job
    if not job:   # a failed job before us can leave the queue sulking: one retry
        time.sleep(2); job = project.AddRenderJob(); r["job_retry"] = job
    r["startrender"] = project.StartRendering([job], False) if job else False
    time.sleep(1.5)
    while project.IsRenderingInProgress():
        time.sleep(0.5)
        if time.time() - t0 > 240: project.StopRendering(); r["timeout"] = True; break
    js = project.GetRenderJobStatus(job) or {} if job else {}
    r["jobstatus"] = js
    try: project.DeleteRenderJob(job)
    except Exception: pass
    files = sorted(os.listdir(vdir)); r["files"] = files; r["seconds"] = round(time.time() - t0, 1)
    tail = log_tail(pos)
    r["logerrors"] = [l for l in tail.splitlines() if any(k in l for k in ("Failed", "could not", "Could not", "Error", "error", "Missing", "not found")) and "subtitle" not in l][:12]
    r["status"] = "PASS" if (js.get("JobStatus") == "Complete" and files and not r.get("timeout")) else "FAIL"
    results[vid] = r; json.dump(results, open(RESULT, "w"), indent=1)
    print(vid, r["status"], js.get("JobStatus"), len(files), "files", r["seconds"], "s")
pm.SaveProject()
print("DONE")
