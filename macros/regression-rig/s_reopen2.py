import time
print("page at start:", resolve.GetCurrentPage())
name = project.GetName() if project else None
print("open project:", name)
if name != "KALYX TEST":
    print("LoadProject:", bool(pm.LoadProject("KALYX TEST")))
    project = pm.GetCurrentProject()
time.sleep(3)
print("page after load:", resolve.GetCurrentPage())
print("OpenPage edit:", resolve.OpenPage("edit"))
time.sleep(3)
print("page now:", resolve.GetCurrentPage())
tl = project.GetCurrentTimeline()
print("timeline:", tl.GetName() if tl else None)
for i in range(1, project.GetTimelineCount() + 1):
    t = project.GetTimelineByIndex(i)
    if t.GetName() == "Kalyx regression":
        print("SetCurrentTimeline:", project.SetCurrentTimeline(t))
        tl = t
mp = project.GetMediaPool()
plate = None
def walk(f):
    global plate
    for c_ in f.GetClipList():
        if c_.GetClipProperty().get("Clip Name", "").startswith("C031_03011654") and plate is None:
            plate = c_
    for s_ in f.GetSubFolderList():
        walk(s_)
walk(mp.GetRootFolder())
a = mp.AppendToTimeline([{"mediaPoolItem": plate, "startFrame": 0, "endFrame": 59}])
print("append:", a)
if a:
    a[0].SetName("probe2")
    print("delete probe:", tl.DeleteClips([a[0]]))
