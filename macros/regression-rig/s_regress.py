# Kalyx macro render regression on the KALYX TEST project. Args (from env): VARIANTS=comma list or all; DRY=1 to skip renders.
import json
import os
import subprocess
import time

HERE = os.path.expanduser("~/kalyx-test/rq")
COMPS = os.path.join(HERE, "regression")
OUTDIR = os.path.join(HERE, "out")
LOG = os.path.expanduser("~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/davinci_resolve.log")
RESULT = os.path.join(HERE, "result.json")
want = os.environ.get("VARIANTS", "all")
dry = os.environ.get("DRY") == "1"
os.makedirs(OUTDIR, exist_ok=True)

assert project.GetName() == "KALYX TEST", project.GetName()
variants = json.load(open(os.path.join(COMPS, "variants.json")))
if want != "all":
    keep = set(want.split(","))
    variants = [v for v in variants if v["id"] in keep]

mp = project.GetMediaPool()
# the scratch timeline
tl = None
for i in range(1, project.GetTimelineCount() + 1):
    t = project.GetTimelineByIndex(i)
    if t.GetName() == "Kalyx regression":
        tl = t
if tl is None:
    tl = mp.CreateEmptyTimeline("Kalyx regression")
    print("created timeline", tl is not None)
project.SetCurrentTimeline(tl)
tl.SetSetting("useCustomSettings", "1")
tl.SetSetting("timelineResolutionWidth", "5760")
tl.SetSetting("timelineResolutionHeight", "2880")
print("timeline", tl.GetName(), tl.GetSetting("timelineResolutionWidth"), "x", tl.GetSetting("timelineResolutionHeight"), "fps", tl.GetSetting("timelineFrameRate"))

# the plate clip: a real clip (generators cannot take an imported comp)
plate = None
def walk(f):
    global plate
    for c_ in f.GetClipList():
        p = c_.GetClipProperty()
        if p.get("Clip Name", "").startswith("C031_03011654") and plate is None:
            plate = c_
    for s_ in f.GetSubFolderList():
        walk(s_)
walk(mp.GetRootFolder())
assert plate, "plate clip not found"
# existing items by name; anything that is not a variant (probe clips, generators) goes
items = {}
ids = {v["id"] for v in json.load(open(os.path.join(COMPS, "variants.json")))}
stray = []
for it in tl.GetItemListInTrack("video", 1):
    if it.GetName() in ids and os.environ.get("FRESH") != "1":
        items[it.GetName()] = it
    else:
        stray.append(it)
if stray:
    print("removing", [s_.GetName() for s_ in stray], tl.DeleteClips(stray))
results = {}
try:
    results = json.load(open(RESULT))
except Exception:
    pass


def log_tail(since_pos):
    try:
        with open(LOG, "rb") as f:
            f.seek(since_pos)
            data = f.read().decode("utf-8", "replace")
        return data
    except Exception as e:
        return "LOGERR %s" % e


def log_pos():
    try:
        return os.path.getsize(LOG)
    except Exception:
        return 0


for v in variants:
    vid = v["id"]
    t0 = time.time()
    pos = log_pos()
    it = items.get(vid)
    if it is None:
        added = mp.AppendToTimeline([{"mediaPoolItem": plate, "startFrame": 0, "endFrame": 59}])
        if not added:
            results[vid] = {"status": "FAIL", "why": "AppendToTimeline returned nothing"}
            continue
        it = added[0]
        it.SetName(vid)
        items[vid] = it
    comp_path = os.path.join(COMPS, v["file"])
    # replace, don't stack: an item keeps every imported comp as a version
    if os.environ.get("KEEP") != "1":
        try:
            for old in list(it.GetFusionCompNameList() or []):
                it.DeleteFusionCompByName(old)
        except Exception as e:
            print("comp cleanup", vid, e)
    comp = it.ImportFusionComp(comp_path)
    if comp:
        try:   # the import becomes a new version; make it the one that renders
            names = it.GetFusionCompNameList() or []
            if names:
                it.LoadFusionCompByName(names[-1])
        except Exception as e:
            print("load comp", vid, e)
    reused = False
    if not comp and it.GetFusionCompCount() > 0:
        # this Resolve session refuses edit-page operations (headless relaunch); render what the item already has
        comp = it.GetFusionCompByIndex(1)
        reused = True
    tools = []
    try:
        if comp:
            tl_ = comp.GetToolList()
            tools = sorted([t.Name for t in tl_.values()])
    except Exception as e:
        tools = ["toollist error %s" % e]
    r = {"desc": v["desc"], "import": bool(comp) and not reused, "reused_existing_comp": reused, "tools": tools[:12], "ntools": len(tools), "start": it.GetStart(), "end": it.GetEnd()}
    if dry or not comp:
        r["status"] = "IMPORTED" if comp else "FAIL"
        results[vid] = r
        json.dump(results, open(RESULT, "w"), indent=1)
        print(vid, r["status"], len(tools), "tools")
        continue
    # render frames 40-41 of this clip, JPEG 1920x960
    vdir = os.path.join(OUTDIR, vid)
    os.makedirs(vdir, exist_ok=True)
    for f in os.listdir(vdir):
        os.unlink(os.path.join(vdir, f))
    project.SetCurrentRenderFormatAndCodec("jpg", "jpg")
    ok = project.SetRenderSettings({"SelectAllFrames": False, "MarkIn": it.GetStart() + 40, "MarkOut": it.GetStart() + 41, "TargetDir": vdir, "CustomName": vid,
                                    "FormatWidth": 1920, "FormatHeight": 960, "ExportVideo": True, "ExportAudio": False})
    r["setrender"] = ok
    job = project.AddRenderJob()
    r["job"] = job
    st = project.StartRendering([job], False)
    r["startrender"] = st
    time.sleep(1.5)
    while project.IsRenderingInProgress():
        time.sleep(0.5)
        if time.time() - t0 > 240:
            project.StopRendering()
            r["timeout"] = True
            break
    js = project.GetRenderJobStatus(job) or {}
    r["jobstatus"] = js
    try:
        project.DeleteRenderJob(job)
    except Exception:
        pass
    files = sorted(os.listdir(vdir))
    r["files"] = files
    r["seconds"] = round(time.time() - t0, 1)
    tail = log_tail(pos)
    bad = [l for l in tail.splitlines() if any(k in l for k in ("No frame available", "Failed to load", "could not", "Could not", "Error", "error", "Missing", "missing", "not found"))]
    r["logerrors"] = bad[:20]
    r["status"] = "PASS" if (js.get("JobStatus") == "Complete" and files and not r.get("timeout")) else "FAIL"
    results[vid] = r
    json.dump(results, open(RESULT, "w"), indent=1)
    print(vid, r["status"], js.get("JobStatus"), len(files), "files", r["seconds"], "s", len(bad), "log lines")

pm.SaveProject()
print("DONE")
