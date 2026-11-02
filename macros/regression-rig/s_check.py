print("PROJECT", project.GetName() if project else None)
print("TIMELINE", tl.GetName() if tl else None)
print("VERSION", resolve.GetVersionString())
n = project.GetTimelineCount()
print("TIMELINES", n, [project.GetTimelineByIndex(i + 1).GetName() for i in range(n)])
mp = project.GetMediaPool()
root = mp.GetRootFolder()
def walk(f, depth=0):
    for c in f.GetClipList():
        p = c.GetClipProperty()
        print("CLIP", "  " * depth, p.get("Clip Name"), p.get("Resolution"), p.get("FPS"), "online" if p.get("File Path") and os.path.exists(p.get("File Path")) else "OFFLINE", p.get("File Path"))
    for s in f.GetSubFolderList():
        print("FOLDER", "  " * depth, s.GetName())
        walk(s, depth + 1)
walk(root)
