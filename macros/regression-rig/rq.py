#!/usr/bin/env python3
"""Run a snippet against the Resolve on this Mac. Usage: rq.py <file.py>  (the snippet sees resolve, pm, project, tl, fu)."""
import os
import sys

API = "/Library/Application Support/Blackmagic Design/DaVinci Resolve/Developer/Scripting"
os.environ.setdefault("RESOLVE_SCRIPT_API", API)
os.environ.setdefault("RESOLVE_SCRIPT_LIB", "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Libraries/Fusion/fusionscript.so")
sys.path.append(API + "/Modules")
import DaVinciResolveScript as dvr  # noqa: E402

resolve = dvr.scriptapp("Resolve")
if not resolve:
    print("NO_RESOLVE")
    sys.exit(2)
pm = resolve.GetProjectManager()
project = pm.GetCurrentProject()
tl = project.GetCurrentTimeline() if project else None
fu = resolve.Fusion()
code = open(sys.argv[1]).read()
exec(compile(code, sys.argv[1], "exec"))
