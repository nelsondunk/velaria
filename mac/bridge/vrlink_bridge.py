#!/usr/bin/python3
"""VR Link bridge: lets the headset read and change the DaVinci Resolve timeline it is watching.

Speaks newline-delimited JSON on TCP 47812 (USB: via `adb reverse`, Wi-Fi: same host as the video). Talks to
Resolve through its scripting API. Everything it changes goes through Fusion macro inputs on the clips under the
playhead; it never adds, moves or deletes anything on the timeline.

Headset -> bridge:
  {"cmd":"hello"}                              reply: timeline + state + params
  {"cmd":"timeline"}                           full clip list
  {"cmd":"seek","frame":N}                     move the playhead (absolute timeline frame)
  {"cmd":"step","delta":±N}
  {"cmd":"clip","dir":±1}                      jump to the next/previous clip edge on the macro tracks
  {"cmd":"params"}                             controls of the macros under the playhead
  {"cmd":"set","clip":CLIP,"tool":TOOL,"input":ID,"value":V}   coalesced, applied at <= 15 Hz
  {"cmd":"undo"}                               restore the last changed input to its value before this session touched it
Bridge -> headset:
  {"type":"timeline", ...}  {"type":"state", frame, tc}  {"type":"params", ...}  {"type":"error", msg}
  {"type":"hello", bridge, installed, resolve, project}   versions, first reply to hello (shown on the View tab)
  The timeline message also carries "format" (timeline and output size, fps, custom flag) and "warnings": the
  setup mistakes Resolve's own settings reveal (square 360 without output scaling). The headset adds the ones only
  it can see (raster and frame rate from the driver, no picture) and shows them in the status line.
"""
import json
import re
import os
import socket
import sys
import threading
import time
import traceback

RESOLVE_API = "/Library/Application Support/Blackmagic Design/DaVinci Resolve/Developer/Scripting"
os.environ.setdefault("RESOLVE_SCRIPT_API", RESOLVE_API)
os.environ.setdefault("RESOLVE_SCRIPT_LIB",
                      "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Libraries/Fusion/fusionscript.so")
sys.path.append(RESOLVE_API + "/Modules")

BRIDGE_VERSION = "1.0.8"
PORT = int(os.environ.get("VRLINK_BRIDGE_PORT", "47812"))
LOG = os.path.expanduser("~/Library/Logs/VRLink/bridge.log")

# Macro inputs worth showing. Everything else (connections, nests, comments, layer selectors) stays hidden.
HIDDEN_IDS = {"HideInputs", "CommentsNest", "Comments", "OutputWidth", "Blend", "ProcessRed", "ProcessGreen",
              "ProcessBlue", "ProcessAlpha", "ApplyMaskInverted", "MultiplyByMask", "FitMask", "MaskChannel",
              "MaskLow", "MaskHigh", "MaskClipBlack", "MaskClipWhite", "EffectMask", "SettingsNest"}


# Resolve's monitor raster tops out at 7680 x 4320; anything taller is cropped, so a square (over/under) 360
# timeline has to be scaled on output. These are the mistakes a new user makes that scripting can see.
MONITOR_MAX_W, MONITOR_MAX_H = 7680, 4320


def preflight(fmt):
    """One short line per setup problem, with the fix in Resolve's own menu words. fmt is timeline_format()."""
    out = []
    w, h, ow, oh = fmt.get("width", 0), fmt.get("height", 0), fmt.get("outWidth", 0), fmt.get("outHeight", 0)
    if ow > MONITOR_MAX_W or oh > MONITOR_MAX_H:
        if ow == oh:
            fit = "3840 x 3840"   # square 360: the value the setup guide gives
        else:
            k = min(MONITOR_MAX_W / float(ow), MONITOR_MAX_H / float(oh))
            fit = "%d x %d" % (int(ow * k) // 16 * 16, int(oh * k) // 16 * 16)
        what = "Timeline %dx%d" % (w, h) if (ow, oh) == (w, h) else "Output scaling %dx%d" % (ow, oh)
        out.append("%s exceeds the %dx%d monitor: Timeline Settings > Output scaling > %s"
                   % (what, MONITOR_MAX_W, MONITOR_MAX_H, fit))
    return out


def log(msg):
    line = "%s %s\n" % (time.strftime("%H:%M:%S"), msg)
    sys.stderr.write(line)
    try:
        os.makedirs(os.path.dirname(LOG), exist_ok=True)
        with open(LOG, "a") as f:
            f.write(line)
    except OSError:
        pass


def tc_to_frame(tc, fps, drop):
    h, m, s, f = [int(x) for x in tc.replace(";", ":").split(":")]
    nominal = int(round(fps))
    frames = ((h * 60 + m) * 60 + s) * nominal + f
    if drop:
        total_min = h * 60 + m
        frames -= 2 * (total_min - total_min // 10)
    return frames


class TransientError(Exception):
    """Resolve gave an incomplete answer (page switch in progress); try again next tick."""


def frame_to_tc(frame, fps, drop):
    nominal = int(round(fps))
    if drop:
        # standard SMPTE drop-frame reconstruction
        per_min = nominal * 60 - 2
        per_10 = per_min * 10 + 2
        tens = frame // per_10
        rem = frame % per_10
        if rem < 2:
            ones = 0
        else:
            ones = (rem - 2) // per_min + 1
        frame += 2 * (tens * 9 + ones)
    f = frame % nominal
    s = (frame // nominal) % 60
    m = (frame // (nominal * 60)) % 60
    h = frame // (nominal * 3600)
    sep = ";" if drop else ":"
    return "%02d:%02d:%02d%s%02d" % (h, m, s, sep, f)


class Bridge:
    def __init__(self):
        self.lock = threading.RLock()
        self.resolve = None
        self.project = None
        self.timeline = None
        self.fps = 30.0
        self.drop = False
        self.clients = []
        self.pending = {}          # (clip, tool, input) -> value, applied by the worker
        self.originals = {}        # (clip, tool, input) -> value before this session touched it
        self.history = []          # keys in change order, for undo
        self.ranges = {}           # (clip, tool, input) -> widest slider range shown this session
        self.last_frame = None
        self.tl_signature = None
        self.comp_cache = {}       # clip key -> (item, comp, {tool name: tool})
        self.clip_index = None     # [(track, item, start, end)] for the Fusion clips, see rebuild_index
        self.animated = {}         # (tool name, input id) -> bool, so a set doesn't re-read the input list

    # ---------------------------------------------------------------- resolve
    def versions(self):
        """Who is on the Mac side: this bridge, the installed Kalyx package (VERSION beside the bridge, written by
        install.sh), Resolve and the project. The headset shows it on the View tab so a bug report names its parts."""
        installed = ""
        try:
            installed = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "VERSION")).read().strip()
        except Exception:
            pass
        resolve = project = ""
        try:
            if self.resolve:
                resolve = str(self.resolve.GetVersionString() or "")
            if self.project:
                project = str(self.project.GetName() or "")
        except Exception:
            pass
        return {"type": "hello", "bridge": BRIDGE_VERSION, "installed": installed, "resolve": resolve, "project": project}

    def connect(self):
        try:
            import DaVinciResolveScript as dvr
            r = dvr.scriptapp("Resolve")
            if not r:
                return False
            pm = r.GetProjectManager()
            p = pm.GetCurrentProject() if pm else None
            if not p:
                return False
            self.resolve, self.project = r, p
            return self.refresh_timeline()
        except Exception as e:
            log("connect failed: %s" % e)
            return False

    def refresh_timeline(self):
        tl = self.project.GetCurrentTimeline()
        if not tl:
            return False
        self.timeline = tl
        try:
            fps = float(tl.GetSetting("timelineFrameRate") or self.project.GetSetting("timelineFrameRate") or 30)
        except Exception:
            fps = 30.0
        self.fps = fps
        drop = tl.GetSetting("timelineDropFrameTimecode") == "1"
        self.drop = drop
        self.format = self.timeline_format(tl)
        warnings = preflight(self.format)
        if warnings != getattr(self, "warnings", None):
            self.warnings = warnings
            for w in warnings:
                log("preflight: " + w)
        return True

    def timeline_format(self, tl):
        """What Resolve will play out for this timeline: its own size, the output scaling, frame rate."""
        def num(getter, key, default=0):
            try:
                v = getter(key)
                return int(float(v)) if v not in (None, "") else default
            except Exception:
                return default
        custom = (tl.GetSetting("useCustomSettings") or "0") == "1"
        g = tl.GetSetting if custom else self.project.GetSetting
        w, h = num(g, "timelineResolutionWidth"), num(g, "timelineResolutionHeight")
        match = (g("timelineOutputResMatchTimelineRes") or "1") == "1"
        ow, oh = (w, h) if match else (num(g, "timelineOutputResolutionWidth", w), num(g, "timelineOutputResolutionHeight", h))
        if not ow or not oh:
            ow, oh = w, h
        return {"width": w, "height": h, "outWidth": ow, "outHeight": oh, "fps": self.fps, "custom": custom}

    def clip_key(self, track, item):
        start = item.GetStart()
        if start is None:   # Resolve answers None for a moment while switching pages/timelines
            raise TransientError("clip has no start yet")
        return "V%d:%d" % (track, start)

    def timeline_snapshot(self):
        tl = self.timeline
        clips = []
        for v in range(1, tl.GetTrackCount("video") + 1):
            for it in tl.GetItemListInTrack("video", v) or []:
                name = it.GetName()
                if name == "Cross Dissolve" or it.GetDuration() <= 0:
                    continue
                clips.append({"key": self.clip_key(v, it), "track": v, "name": name, "start": it.GetStart(),
                              "end": it.GetEnd(), "color": it.GetClipColor() or "",
                              "fusion": it.GetFusionCompCount() > 0})
        markers = []
        for frame, m in (tl.GetMarkers() or {}).items():
            markers.append({"frame": tl.GetStartFrame() + int(frame), "name": m.get("name", ""),
                            "color": m.get("color", "")})
        return {"type": "timeline", "name": tl.GetName(), "start": tl.GetStartFrame(), "end": tl.GetEndFrame(),
                "fps": self.fps, "drop": self.drop, "tracks": tl.GetTrackCount("video"), "clips": clips,
                "markers": markers, "format": getattr(self, "format", {}), "warnings": getattr(self, "warnings", [])}

    def signature(self):
        tl = self.timeline
        sig = [tl.GetName(), tl.GetStartFrame(), tl.GetEndFrame(), repr(getattr(self, "format", None)), tuple(getattr(self, "warnings", []))]
        for v in range(1, tl.GetTrackCount("video") + 1):
            items = tl.GetItemListInTrack("video", v) or []
            sig.append((v, len(items), items[-1].GetEnd() if items else 0))
        return tuple(sig)

    def current_frame(self):
        # Resolve reports no timecode on some pages (Fusion, Media, Deliver): keep the last known position.
        tc = self.timeline.GetCurrentTimecode()
        if not tc:
            f = self.fusion_page_frame()
            if f is not None:
                self.last_frame = f
                return f
            return self.last_frame if self.last_frame is not None else self.timeline.GetStartFrame()
        return tc_to_frame(tc, self.fps, self.drop)

    def fusion_page_frame(self):
        # On the Fusion page the playhead is the current comp's time: timeline frame = clip start + (CurrentTime - GlobalStart).
        try:
            item = self.timeline.GetCurrentVideoItem()
            if not item or not item.GetFusionCompCount():
                return None
            comp = item.GetFusionCompByIndex(1)
            if not comp:
                return None
            attrs = comp.GetAttrs()
            cur = attrs.get("COMPN_CurrentTime")
            gs = attrs.get("COMPN_GlobalStart")
            if cur is None or gs is None:
                return None
            return int(item.GetStart()) + int(round(cur - gs))
        except Exception:
            return None

    def state(self):
        frame = self.current_frame()
        return {"type": "state", "frame": frame, "tc": frame_to_tc(frame, self.fps, self.drop)}

    def rebuild_index(self):
        """One pass over the timeline (4 calls per clip) so the per-tick playhead lookup costs nothing. Rebuilt
        every couple of seconds; on a 180-clip timeline the live scan took ~100 ms and ran four times a second."""
        tl = self.timeline
        index = []
        for v in range(tl.GetTrackCount("video"), 0, -1):   # top track first
            for it in tl.GetItemListInTrack("video", v) or []:
                start, end = it.GetStart(), it.GetEnd()
                if start is None or end is None:
                    raise TransientError("clip has no extent yet")
                if it.GetFusionCompCount() > 0 and it.GetName() != "Cross Dissolve":
                    index.append((v, it, start, end))
        self.clip_index = index

    def items_under_playhead(self):
        frame = self.current_frame()
        if self.clip_index is None:
            self.rebuild_index()
        return [(v, it) for v, it, start, end in self.clip_index if start <= frame < end]

    # Fusion tools (besides the Kalyx macros) worth driving from the headset, with the inputs they expose.
    # PanoMap: re-orient a whole sphere (fix a wonky horizon); LatLongPatcher: the nadir/zenith patch window.
    TOOL_INPUTS = {
        "PanoMap": ("Rotate.X", "Rotate.Y", "Rotate.Z"),
        "LatLongPatcher": ("Rotate.X", "Rotate.Y", "Rotate.Z", "AoV"),
    }

    # what a tool is to the headset: "card" (a Kalyx macro: pickable, on the CARDS layer), "patch" (a LatLong
    # Patcher window) or "orient" (a PanoMap turning the whole sphere); patch + orient live on the PATCHES layer
    TOOL_KIND = {"PanoMap": "orient", "LatLongPatcher": "patch"}

    def edit_tools(self, comp):
        """The tools of a comp the headset gets controls for: every macro, plus the whitelisted stock tools."""
        out = []
        inside = set()   # tools living inside a macro (GetToolList also returns those): the macro is the control
        for _, t in (comp.GetToolList(False, "MacroOperator") or {}).items():
            # Kalyx Orient 360 (split → PanoMap per eye → combine) turns the whole plate: PICTURE layer, not a card.
            # Kalyx Trim (a ColorCorrector on an adjustment clip) grades the whole picture: PICTURE layer too, kind "trim".
            kind = "orient" if t.Name.startswith("Kalyx_Orient") else "trim" if t.Name.startswith("Kalyx_Trim") else "card"
            out.append((t, None, kind))
            try:
                for _, c in (t.GetChildrenList() or {}).items():
                    inside.add(c.Name)
            except Exception:
                pass
        for reg, allowed in self.TOOL_INPUTS.items():
            for _, t in (comp.GetToolList(False, reg) or {}).items():
                if t.Name in inside:
                    continue
                out.append((t, set(allowed), self.TOOL_KIND.get(reg, "patch")))
        return out

    def macro_params(self):
        """Controls of every macro (and whitelisted stock tool) on the clips under the playhead."""
        groups = []
        for v, it in self.items_under_playhead():
            key = self.clip_key(v, it)
            try:
                comp = it.GetFusionCompByIndex(1)
                if not comp:
                    continue
                tools = self.edit_tools(comp)
            except Exception as e:
                log("comp of %s: %s" % (key, e))
                continue
            for tool, allowed, gkind in tools:
                params = []
                for _, inp in (tool.GetInputList() or {}).items():
                    a = inp.GetAttrs()
                    pid = a.get("INPS_ID")
                    dt = a.get("INPS_DataType")
                    ctl = a.get("INPID_InputControl") or ""
                    if allowed is not None and pid not in allowed:
                        continue
                    if pid in HIDDEN_IDS or dt not in ("Number", "Text", "Point") or "Nest" in ctl or "Layer" in ctl:
                        continue
                    if dt == "Text" and ctl != "TextEditControl":
                        continue
                    try:
                        val = tool.GetInput(pid)
                    except Exception:
                        continue
                    kind = "text" if dt == "Text" else "point" if dt == "Point" else \
                        "bool" if ctl == "CheckboxControl" else "number"
                    if dt == "Point":
                        try:
                            val = [float(val[1]), float(val[2])]
                        except Exception:
                            val = [0.5, 0.5]
                    elif kind != "text":
                        try:
                            val = float(val)
                        except Exception:
                            continue
                    params.append({"id": pid, "name": a.get("INPS_Name") or pid, "kind": kind,
                                   "min": a.get("INPN_MinScale", 0.0), "max": a.get("INPN_MaxScale", 1.0),
                                   "value": val, "animated": bool(inp.GetKeyFrames())})
                if allowed is None:
                    params += self.virtual_params(tool, {q["id"] for q in params})
                params = self.organise(params)
                # slider ranges only ever widen during a session, so dragging a value back inside the macro's
                # suggested range never takes away the room it had (e.g. a Distance someone set beyond the scale)
                for q in params:
                    if q["kind"] != "number":
                        continue
                    rk = (key, tool.Name, q["id"])
                    lo, hi = self.ranges.get(rk, (q["min"], q["max"]))
                    q["min"], q["max"] = min(lo, q["min"]), max(hi, q["max"])
                    self.ranges[rk] = (q["min"], q["max"])
                if params:
                    groups.append({"clip": key, "clipName": it.GetName(), "track": v, "tool": tool.Name, "kind": gkind,
                                   "params": params, "source": self.comp_source(it, comp)})
                self.comp_cache[key] = (it, comp, {t.Name: t for t, _, _ in tools})
        order = {"card": 0, "orient": 1, "trim": 2, "patch": 3}
        groups.sort(key=lambda g: order.get(g["kind"], 3))
        return {"type": "params", "frame": self.current_frame(), "groups": groups}

    # Controls the macro doesn't publish but its inner tools have. Reached through GetChildrenList(), so every
    # FactWindow already in a comp gets them without re-inserting the macro. id -> (name, min, max, [(child, input)])
    VIRTUALS = {
        "@size": ("Size", 0.25, 6.0, [("Left_Window_Plane", "Transform3DOp.Scale.X")]),
        "@tilt": ("Object Tilt", -90.0, 90.0, [("Left_Window_Plane", "Transform3DOp.Rotate.X"),
                                               ("Right_Window_Plane", "Transform3DOp.Rotate.X")]),
        "@turn": ("Object Turn", -90.0, 90.0, [("Left_Window_Plane", "Transform3DOp.Rotate.Y"),
                                              ("Right_Window_Plane", "Transform3DOp.Rotate.Y")]),
        "@spin": ("Object Spin", -180.0, 180.0, [("Left_Window_Plane", "Transform3DOp.Rotate.Z"),
                                                ("Right_Window_Plane", "Transform3DOp.Rotate.Z")]),
    }
    # published inputs that make a virtual one redundant (newer macro versions)
    VIRTUAL_PUBLISHED = {"@tilt": "ObjectTilt", "@turn": "ObjectTurn", "@spin": "ObjectSpin", "@size": "Size"}

    def children(self, tool):
        # Fusion renames inner tools when a comp holds several copies of a macro (Left_Window_Plane_1 ...)
        try:
            return {re.sub(r"_\d+$", "", c.Name): c for c in (tool.GetChildrenList() or {}).values()}
        except Exception:
            return {}

    def virtual_params(self, tool, published):
        out = []
        kids = self.children(tool)
        for vid, (name, lo, hi, targets) in self.VIRTUALS.items():
            if self.VIRTUAL_PUBLISHED.get(vid) in published:
                continue
            first = kids.get(targets[0][0])
            if not first or any(t[0] not in kids for t in targets):
                continue
            try:
                val = float(first.GetInput(targets[0][1]))
            except Exception:
                continue
            out.append({"id": vid, "name": name, "kind": "number", "min": lo, "max": hi, "value": val,
                        "animated": self.is_animated(first, targets[0][1])})
        return out

    def targets(self, tool, pid):
        """(tool, input) pairs that a control writes to: the macro input itself, or inner tools for a virtual one."""
        if not pid.startswith("@"):
            return [(tool, pid)]
        spec = self.VIRTUALS.get(pid)
        if not spec:
            return []
        kids = self.children(tool)
        out = [(kids[c], i) for c, i in spec[3] if c in kids]
        if pid == "@size" and out:
            plane = out[0][0]
            try:
                locked = plane.GetInput("Transform3DOp.ScaleLock")
            except Exception:
                locked = 1
            if not locked:   # unlocked scale: drive all three axes
                out += [(plane, "Transform3DOp.Scale.Y"), (plane, "Transform3DOp.Scale.Z")]
        return out

    def get_input(self, tool, pid):
        tg = self.targets(tool, pid)
        return tg[0][0].GetInput(tg[0][1]) if tg else None

    # Display names for the headset, by input id (the macro's own labels stay as they are in Fusion).
    DISPLAY_NAMES = {
        "Yaw": "Pan", "Pitch": "Tilt", "Roll": "Roll", "Distance": "Distance",
        "@size": "Scale", "Size": "Scale",
        "ObjectTilt": "Pitch (card)", "ObjectTurn": "Yaw (card)", "ObjectSpin": "Roll (card)",
        "@tilt": "Pitch (card)", "@turn": "Yaw (card)", "@spin": "Roll (card)",
        "FrameColour": "Border colour", "FrameThickness": "Border width",
        "CornerRadius": "Corner radius",
        "WindowSize": "Image fill", "CropSides": "Crop sides", "CropTopBottom": "Crop top & bottom",
        "WindowAspect": "Window aspect", "Source": "Window shows (0 footage, 1 graphic, 2 none)",
        "WindowDepth": "Stereo depth",
        "GlowSize": "Glow size", "GlowBrightness": "Glow brightness", "GlowAmount": "Glow opacity",
        "Mode": "Stereo source (L/R)",
        "OutputWidth": "Render width (per eye)",
        # PanoMap / LatLongPatcher
        "Rotate.X": "Tilt", "Rotate.Y": "Pan", "Rotate.Z": "Roll", "AoV": "Window angle",
        # Kalyx Trim
        "Exposure": "Exposure", "Lift": "Lift", "Gamma": "Gamma", "Gain": "Gain", "Contrast": "Contrast",
        "Saturation": "Saturation", "Temperature": "Temperature", "Tint": "Tint", "Amount": "Amount",
    }
    # Section names: Fusion page/word -> what the tab says
    SECTION_NAMES = {"Frame": "Border", "Window": "Image", "Settings": "Output", "Glow": "Glow", "Stereo": "Stereo", "Trim": "Trim"}
    # ids that belong to a section regardless of their (renamed) first word
    SECTION_BY_ID = {
        "FrameColour": "Border", "FrameThickness": "Border", "CornerRadius": "Border",
        "WindowSize": "Image", "CropSides": "Image", "CropTopBottom": "Image", "WindowAspect": "Image", "Source": "Image", "GlowSize": "Glow", "GlowBrightness": "Glow", "GlowAmount": "Glow",
        "WindowDepth": "Stereo", "Mode": "Stereo", "OutputWidth": "Output",
        "Rotate.X": "Placement", "Rotate.Y": "Placement", "Rotate.Z": "Placement", "AoV": "Patch",
        "Exposure": "Trim", "Lift": "Trim", "Gamma": "Trim", "Gain": "Trim", "Contrast": "Trim", "Saturation": "Trim",
        "Temperature": "Trim", "Tint": "Trim", "Amount": "Trim",
    }

    PLACEMENT = ("yaw", "pitch", "roll", "pan", "tilt", "distance", "depth", "rotate", "rotation", "spin",
                 "position", "size", "scale", "offset")

    def organise(self, params):
        """Merges Fusion colour triplets (X, XG, XB) into one colour control and files every control in a section."""
        ids = {q["id"]: q for q in params}
        merged = []
        for q in params:
            pid = q["id"]
            if pid[-1:] in ("G", "B") and pid[:-1] in ids and ids[pid[:-1]]["kind"] == "number" \
                    and pid[:-1] + "G" in ids and pid[:-1] + "B" in ids:
                continue   # folded into the colour below
            if q["kind"] == "number" and pid + "G" in ids and pid + "B" in ids:
                g, b = ids[pid + "G"], ids[pid + "B"]
                name = q["name"]
                for suffix in (" \u00b7 R", " Red", " R"):
                    if name.endswith(suffix):
                        name = name[:-len(suffix)]
                q = dict(q, kind="colour", name=name, ids=[pid, g["id"], b["id"]],
                         value=[q["value"], g["value"], b["value"]], min=0.0, max=1.0)
            merged.append(q)
        # Fusion's Y rotation is counter-clockwise-positive seen from above, so a bigger yaw/pan moves the object to the
        # LEFT. Sliders run the other way for these so dragging right moves it right (the value itself is unchanged).
        for q in merged:
            low = q["name"].lower()
            if q["kind"] == "number" and (q["id"].lower() in ("yaw", "pan") or low.startswith("yaw") or low.startswith("pan")):
                q["invert"] = True
        for q in merged:
            if q["kind"] != "number":
                continue
            name = q["name"]
            # "Mono (0) / Stereo (1)" style inputs are switches, not sliders
            if re.search(r"\(0\).*\(1\)", name) and float(q["min"]) == 0.0 and float(q["max"]) == 1.0:
                q["kind"] = "bool"
                q["name"] = re.sub(r"\s*\(\d\)", "", name).replace(" / ", " / ")
                if "stereo" in name.lower():
                    q["name"] = "Stereo source (L/R)"
            # slider ranges are only the macro's suggested scale; never clamp a value someone typed beyond them
            v = q["value"]
            if isinstance(v, (int, float)):
                lo, hi = float(q["min"]), float(q["max"])
                if v < lo or v > hi:
                    span = hi - lo
                    q["min"] = min(lo, v - 0.1 * span if v < lo else lo)
                    q["max"] = max(hi, v + 0.1 * span if v > hi else hi)
                    # keep log sliders on one side of zero
                    if lo < 0 and hi < 0 and q["max"] >= 0:
                        q["max"] = v * 0.5
                    if lo > 0 and hi > 0 and q["min"] <= 0:
                        q["min"] = v * 0.5
        # section = first word of the name when at least two controls share it, else Placement/Settings
        def first_word(q):
            w = q["name"].split(" ")[0].strip("():,")
            return w if len(w) > 2 and not w[0].isdigit() else ""
        counts = {}
        for q in merged:
            w = first_word(q)
            if w:
                counts[w] = counts.get(w, 0) + 1
        for q in merged:
            w = first_word(q)
            low = (q["name"] + " ").lower()
            if any(k in low for k in ("stereo", "depth", "convergence", "interaxial", "parallax", "mono")):
                q["section"] = "Stereo"
            elif w and counts.get(w, 0) >= 2 and w.lower() not in ("the", "use", "show"):
                q["section"] = w
            elif any(low.startswith(k) or (" " + k) in low for k in self.PLACEMENT):
                q["section"] = "Placement"
            else:
                q["section"] = "Settings"
        # friendly names and tab names
        for q in merged:
            if q["id"] in self.SECTION_BY_ID:
                q["section"] = self.SECTION_BY_ID[q["id"]]
            else:
                q["section"] = self.SECTION_NAMES.get(q["section"], q["section"])
            if q["id"] in self.DISPLAY_NAMES:
                q["name"] = self.DISPLAY_NAMES[q["id"]]
        # Placement first, then sections in order of appearance, Output last
        order = []
        for q in merged:
            if q["section"] not in order:
                order.append(q["section"])
        order.sort(key=lambda sname: 0 if sname == "Placement" else 1 if sname == "Object" else 2 if sname == "Stereo" else 4 if sname in ("Settings", "Output") else 3)
        merged.sort(key=lambda q: order.index(q["section"]))
        return merged

    def comp_source(self, it, comp):
        """What this card is made of: the comp's name when it has a real one, and the footage files its MediaIn /
        Loader nodes read (a stereo L/R pair collapsed to one name)."""
        parts = []
        try:
            names = it.GetFusionCompNameList() or []
            if names and not re.match(r"^Composition \d+$", names[0] or ""):
                parts.append(names[0])
        except Exception:
            pass
        files = []
        try:
            for t in (comp.GetToolList(False) or {}).values():
                a = t.GetAttrs()
                reg = a.get("TOOLS_RegID")
                nm = None
                if reg == "MediaIn":
                    nm = a.get("TOOLS_Clip_Name")
                elif reg == "Loader":
                    nm = os.path.basename(str(t.GetInput("Clip") or ""))
                if nm and nm not in files:
                    files.append(nm)
        except Exception:
            pass
        # collapse "Name L.mov" + "Name R.mov" -> "Name L/R.mov"
        pairs = {}
        for f in files:
            m = re.match(r"^(.*?)[ _-]?([LR])(\.[A-Za-z0-9]+)?$", f)
            k = (m.group(1), m.group(3) or "") if m else (f, "")
            pairs.setdefault(k, set()).add(m.group(2) if m else "")
        for (base, ext), eyes in pairs.items():
            if eyes >= {"L", "R"}:
                parts.append(base + " L/R" + ext)
            else:
                eye = "".join(sorted(e for e in eyes if e))
                parts.append(base + (" " + eye if eye else "") + ext)
        return "  \u00b7  ".join(parts)

    def comp_time(self, it, comp):
        """The comp-local time of the timeline playhead for this clip (keyframes go here)."""
        try:
            gs = comp.GetAttrs().get("COMPN_GlobalStart")
            if gs is None:
                return None
            return float(gs) + float(self.current_frame() - int(it.GetStart()))
        except Exception:
            return None

    def is_animated(self, tool, pid):
        k = (tool.Name, pid)
        if k in self.animated:
            return self.animated[k]
        val = False
        try:
            for _, inp in (tool.GetInputList() or {}).items():
                if inp.GetAttrs().get("INPS_ID") == pid:
                    val = bool(inp.GetKeyFrames())
                    break
        except Exception:
            pass
        self.animated[k] = val
        return val

    def set_input(self, it, comp, tool, pid, value):
        if isinstance(value, list):
            value = {1: float(value[0]), 2: float(value[1])}
        # an animated input takes the value as a keyframe at the playhead; a static one just takes the value
        for tt, iid in self.targets(tool, pid):
            t = self.comp_time(it, comp) if self.is_animated(tt, iid) else None
            if t is None:
                tt.SetInput(iid, value)
            else:
                tt.SetInput(iid, value, t)

    def apply_pending(self):
        with self.lock:
            batch, self.pending = self.pending, {}
        # several inputs of one comp (pan + tilt from a grab) go in under a comp lock so Resolve renders once
        locked = set()
        try:
            for (clip, toolname, pid), value in batch.items():
                cached = self.comp_cache.get(clip)
                if not cached:
                    continue
                it, comp, tools = cached
                tool = tools.get(toolname)
                if not tool:
                    continue
                if len(batch) > 1 and clip not in locked:
                    try:
                        comp.Lock()
                        locked.add(clip)
                    except Exception:
                        pass
                k = (clip, toolname, pid)
                try:
                    if k not in self.originals:
                        self.originals[k] = self.get_input(tool, pid)
                        self.history.append(k)
                    t0 = time.time()
                    self.set_input(it, comp, tool, pid, value)
                    log("set %s.%s = %r (%.0f ms)" % (toolname, pid, value, (time.time() - t0) * 1000))
                except Exception as e:
                    log("set %s.%s: %s" % (toolname, pid, e))
                    self.broadcast({"type": "error", "msg": "Couldn't set %s" % pid})
        finally:
            for clip in locked:
                try:
                    self.comp_cache[clip][1].Unlock()
                except Exception:
                    pass

    def paste(self, msg):
        """Applies many inputs at once (Copy/Paste between instances); one Undo reverts the whole paste."""
        cached = self.comp_cache.get(msg.get("clip"))
        if not cached:
            return 0
        it, comp, tools = cached
        tool = tools.get(msg.get("tool"))
        if not tool:
            return 0
        done = []
        comp.Lock()
        try:
            for pid, value in (msg.get("values") or {}).items():
                try:
                    prev = self.get_input(tool, pid)
                    self.set_input(it, comp, tool, pid, value)
                    done.append(((msg["clip"], msg["tool"], pid), prev))
                except Exception as e:
                    log("paste %s.%s: %s" % (tool.Name, pid, e))
        finally:
            comp.Unlock()
        if done:
            self.history.append({"group": done})
        log("paste %d inputs into %s on %s" % (len(done), msg.get("tool"), msg.get("clip")))
        return len(done)

    def undo(self):
        while self.history:
            k = self.history.pop()
            if isinstance(k, dict):
                for (clip, toolname, pid), prev in reversed(k["group"]):
                    cached = self.comp_cache.get(clip)
                    tool = cached[2].get(toolname) if cached else None
                    if tool and prev is not None:
                        for tt, iid in self.targets(tool, pid):
                            tt.SetInput(iid, prev)
                return "paste"
            orig = self.originals.pop(k, None)
            cached = self.comp_cache.get(k[0])
            if cached and orig is not None:
                tool = cached[2].get(k[1])
                if tool:
                    for tt, iid in self.targets(tool, k[2]):
                        tt.SetInput(iid, orig)
                    return k[2].lstrip("@")
        return None

    # ---------------------------------------------------------------- commands
    def handle(self, client, msg):
        cmd = msg.get("cmd")
        if cmd == "hello":
            self.send(client, self.versions())
            self.send(client, self.timeline_snapshot())
            self.send(client, self.state())
            self.send(client, self.macro_params())
        elif cmd == "timeline":
            self.send(client, self.timeline_snapshot())
        elif cmd == "params":
            self.send(client, self.macro_params())
        elif cmd == "seek":
            self.seek(int(msg["frame"]))
        elif cmd == "step":
            self.seek(self.current_frame() + int(msg.get("delta", 1)))
        elif cmd == "clip":
            self.jump_clip(int(msg.get("dir", 1)))
        elif cmd == "set":
            # applied at once (the caller holds the lock): the headset sends one change per Resolve render, so
            # waiting for the worker tick only added up to 70 ms to every drag step
            with self.lock:
                self.pending[(msg["clip"], msg["tool"], msg["input"])] = msg["value"]
                self.apply_pending()
        elif cmd == "paste":
            with self.lock:
                n = self.paste(msg)
            self.broadcast({"type": "notice", "msg": "Pasted %d settings" % n if n else "Nothing to paste here"})
            self.broadcast(self.macro_params())
        elif cmd == "key":
            self.keyframe(msg)
        elif cmd == "undo":
            pid = self.undo()
            log("undo -> %s" % (pid or "nothing"))
            self.broadcast(self.macro_params())
            self.broadcast({"type": "notice", "msg": ("Undid paste" if pid == "paste" else "Restored " + pid) if pid else "Nothing to undo"})
        elif cmd == "ping":
            self.send(client, {"type": "pong", "t": msg.get("t")})

    def keyframe(self, msg):
        cached = self.comp_cache.get(msg.get("clip"))
        if not cached:
            return
        it, comp, tools = cached
        tool = tools.get(msg.get("tool"))
        if not tool:
            return
        t = self.comp_time(it, comp)
        if t is None:
            self.broadcast({"type": "error", "msg": "Can't find the playhead in this comp"})
            return
        done = []
        for pid, value in zip(msg.get("inputs", []), msg.get("values", [])):
            try:
                for tt, iid in self.targets(tool, pid):
                    if not self.is_animated(tt, iid):
                        tt.AddModifier(iid, "BezierSpline")
                        self.animated[(tt.Name, iid)] = True
                    tt.SetInput(iid, value, t)
                done.append(pid)
                log("key %s.%s = %r @ %s" % (tool.Name, pid, value, t))
            except Exception as e:
                log("key %s.%s: %s" % (tool.Name, pid, e))
                self.broadcast({"type": "error", "msg": "Couldn't key %s" % pid})
        if done:
            self.broadcast({"type": "notice", "msg": "Keyframe set at %s" % frame_to_tc(self.current_frame(), self.fps, self.drop)})
            self.broadcast(self.macro_params())

    def seek(self, frame):
        tl = self.timeline
        frame = max(tl.GetStartFrame(), min(tl.GetEndFrame() - 1, frame))
        tl.SetCurrentTimecode(frame_to_tc(frame, self.fps, self.drop))

    def jump_clip(self, direction):
        frame = self.current_frame()
        edges = set()
        tl = self.timeline
        for v in range(1, tl.GetTrackCount("video") + 1):
            for it in tl.GetItemListInTrack("video", v) or []:
                if it.GetName() != "Cross Dissolve":
                    edges.add(it.GetStart())
        edges = sorted(edges)
        if direction > 0:
            nxt = [e for e in edges if e > frame]
            if nxt:
                self.seek(nxt[0])
        else:
            prv = [e for e in edges if e < frame - 1]
            if prv:
                self.seek(prv[-1])

    # ---------------------------------------------------------------- network
    def send(self, client, obj):
        try:
            client.sendall((json.dumps(obj, separators=(",", ":")) + "\n").encode())
        except OSError:
            self.drop_client(client)

    def broadcast(self, obj):
        with self.lock:
            cs = list(self.clients)
        for c in cs:
            self.send(c, obj)

    def drop_client(self, client):
        with self.lock:
            if client in self.clients:
                self.clients.remove(client)
        try:
            client.close()
        except OSError:
            pass

    def client_loop(self, client, peer):
        log("headset connected from %s:%d" % peer)
        buf = b""
        client.settimeout(None)
        try:
            while True:
                data = client.recv(65536)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    if not line.strip():
                        continue
                    try:
                        msg = json.loads(line)
                    except ValueError:
                        continue
                    with self.lock:
                        if self.timeline is None:
                            self.send(client, {"type": "error", "msg": "Resolve not available"})
                            continue
                        try:
                            self.handle(client, msg)
                        except Exception as e:
                            log("command %s failed: %s\n%s" % (msg, e, traceback.format_exc()))
                            self.send(client, {"type": "error", "msg": str(e)[:120]})
        except OSError:
            pass
        log("headset %s:%d disconnected" % peer)
        self.drop_client(client)

    def accept_loop(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", PORT))
        srv.listen(4)
        log("bridge listening on TCP %d" % PORT)
        while True:
            c, peer = srv.accept()
            c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with self.lock:
                self.clients.append(c)
            threading.Thread(target=self.client_loop, args=(c, peer), daemon=True).start()

    # ---------------------------------------------------------------- main loop
    def run(self):
        threading.Thread(target=self.accept_loop, daemon=True).start()
        next_sig = 0
        next_params = 0
        self.last_keys = None
        while True:
            if self.timeline is None:
                if self.connect():
                    log("connected to Resolve: %s / %s" % (self.project.GetName(), self.timeline.GetName()))
                    self.tl_signature = None
                else:
                    time.sleep(3)
                    continue
            try:
                with self.lock:
                    if self.pending:
                        self.apply_pending()
                    now = time.time()
                    if now >= next_sig:
                        next_sig = now + 2.0
                        self.refresh_timeline()
                        sig = self.signature()
                        self.rebuild_index()
                        if sig != self.tl_signature:
                            self.tl_signature = sig
                            self.comp_cache.clear()
                            self.animated.clear()
                            if self.clients:
                                self.broadcast(self.timeline_snapshot())
                    frame = self.current_frame()
                    if frame != self.last_frame:
                        self.last_frame = frame
                        if self.clients:
                            self.broadcast(self.state())
                    # when the playhead lands on a different set of clips, push their controls (at most 4x/s)
                    if self.clients and now >= next_params:
                        next_params = now + 0.25
                        keys = tuple(self.clip_key(v, it) for v, it in self.items_under_playhead())
                        if keys != self.last_keys:
                            self.last_keys = keys
                            self.broadcast(self.macro_params())
            except TransientError:
                time.sleep(0.25)
                continue
            except Exception as e:
                log("resolve lost: %s\n%s" % (e, traceback.format_exc()))
                self.timeline = None
                self.project = None
                time.sleep(2)
                continue
            time.sleep(0.07)


if __name__ == "__main__":
    Bridge().run()
