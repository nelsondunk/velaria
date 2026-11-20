#!/usr/bin/python3
"""Velaria bridge: lets the headset read and change the DaVinci Resolve timeline it is watching.

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
  Output scaling: while a headset is connected the bridge sets Timeline Settings > Output scaling itself on a
  timeline too large for the monitor raster (square -> 3840 x 3840) and restores it when the last headset leaves;
  the originals live in ~/Library/Application Support/Velaria/output-scaling.json until then, so a bridge that
  dies mid-session restores them on its next start.
"""
import json
import re
import os
import select
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

BRIDGE_VERSION = "1.0.15"
RESTORE_GRACE_S = 5.0   # headset gone this long before a scaled timeline is put back
PORT = int(os.environ.get("VRLINK_BRIDGE_PORT", "47812"))
LOG = os.path.expanduser("~/Library/Logs/Velaria/bridge.log")
CONFIG = os.path.expanduser("~/Library/Application Support/Velaria/config.plist")
LOG_MAX_BYTES = 5 * 1024 * 1024    # bridge.log is moved to bridge.old.log past this size
MAX_LINE = 1024 * 1024             # a command line longer than this is not from a headset: drop the connection
SET_GATHER_S = 0.002               # how long to wait for the rest of a multi-input change (pan + tilt, R + G + B)
MAX_CLIENTS = 8                    # connections kept at once; further ones are closed on arrival
DRAG_QUIET_S = 1.0                 # the periodic timeline re-scan waits until no change has been applied for this long


def bind_address():
    """Which interface the bridge listens on. Default: all of them (Wi-Fi and USB). `Network` = `usb` in
    config.plist (the same key the driver reads) or VRLINK_BRIDGE_BIND=127.0.0.1 keeps it to this Mac, which is all
    the USB cable needs: `adb reverse` connects from localhost."""
    env = os.environ.get("VRLINK_BRIDGE_BIND")
    if env:
        return env
    try:
        import plistlib
        with open(CONFIG, "rb") as f:
            if str(plistlib.load(f).get("Network", "")).strip().lower() == "usb":
                return "127.0.0.1"
    except Exception:
        pass
    return "0.0.0.0"

# Macro inputs worth showing. Everything else (connections, nests, comments, layer selectors) stays hidden.
HIDDEN_IDS = {"HideInputs", "CommentsNest", "Comments", "OutputWidth", "Blend", "ProcessRed", "ProcessGreen",
              "ProcessBlue", "ProcessAlpha", "ApplyMaskInverted", "MultiplyByMask", "FitMask", "MaskChannel",
              "MaskLow", "MaskHigh", "MaskClipBlack", "MaskClipWhite", "EffectMask", "SettingsNest"}


# Resolve's monitor raster tops out at 7680 x 4320; anything taller is cropped, so a square (over/under) 360
# timeline has to be scaled on output. While a headset is connected the bridge sets that scaling itself and puts
# the original back when the last headset leaves (see Bridge.autoscale_*); the preflight line then says so instead
# of asking the user to do it.
MONITOR_MAX_W, MONITOR_MAX_H = 7680, 4320
SIDECAR = os.path.expanduser("~/Library/Application Support/Velaria/output-scaling.json")


def fit_size(ow, oh):
    """The output size the headset path wants for a picture Resolve's monitor raster cannot carry: square stays
    square at 3840 (the encoder's sweet spot, square at any rate), anything else keeps its aspect inside the raster
    on multiples of 16. Returns None when the size already fits."""
    if ow <= MONITOR_MAX_W and oh <= MONITOR_MAX_H:
        return None
    if ow == oh:
        return 3840, 3840
    k = min(MONITOR_MAX_W / float(ow), MONITOR_MAX_H / float(oh))
    return int(ow * k) // 16 * 16, int(oh * k) // 16 * 16


def preflight(fmt, managed=None):
    """One short line per setup problem, with the fix in Resolve's own menu words. fmt is timeline_format().
    managed: the (w, h) the bridge has set for the headset on this timeline, if it has; the line then reports
    that instead of asking for it."""
    out = []
    w, h, ow, oh = fmt.get("width", 0), fmt.get("height", 0), fmt.get("outWidth", 0), fmt.get("outHeight", 0)
    if managed:
        out.append("Output scaling set to %d x %d for the headset (timeline %dx%d); put back when it disconnects"
                   % (managed[0], managed[1], w, h))
        return out
    fit = fit_size(ow, oh)
    if fit:
        what = "Timeline %dx%d" % (w, h) if (ow, oh) == (w, h) else "Output scaling %dx%d" % (ow, oh)
        out.append("%s exceeds the %dx%d monitor: Timeline Settings > Output scaling > %d x %d"
                   % (what, MONITOR_MAX_W, MONITOR_MAX_H, fit[0], fit[1]))
    return out


FW_ACTIVE = "/Library/Frameworks/DeckLinkAPI.framework/Resources/Info.plist"
FW_ASIDE = ("/Library/Application Support/Velaria/Blackmagic/DeckLinkAPI.framework",
            "/Library/Frameworks/DeckLinkAPI.framework.blackmagic-backup")
_driver_check = {"at": 0.0, "msg": None}


def driver_notice(now=None):
    """A line when Blackmagic Desktop Video has been installed or updated over Velaria's driver: the installer set
    Blackmagic's framework aside, but the active one is no longer ours, so Resolve lists no "Velaria (Quest)".
    Checked at most every 30 s (two small file reads)."""
    now = time.time() if now is None else now
    st = _driver_check
    if now - st["at"] < 30:
        return st["msg"]
    st["at"] = now
    msg = None
    try:
        # whatever framework is active, if it is not ours Resolve has no Velaria output (review 17: a plain install
        # with Desktop Video put over it has no aside folder, and used to get no notice)
        if os.path.exists(FW_ACTIVE):
            with open(FW_ACTIVE, "rb") as f:
                if b"skylightproductions" not in f.read():
                    msg = ("Blackmagic Desktop Video replaced Velaria's driver: run  sudo ./install.sh --coexist  "
                           "from the Velaria folder, then restart Resolve")
    except Exception:
        msg = None
    if msg != st["msg"]:
        log("driver: " + (msg or "Velaria's driver is back in place"))
    st["msg"] = msg
    return msg


_logf = {"path": None, "f": None, "n": 0}


def log(msg, echo=True):
    """One line to bridge.log, and to stderr (bridge.out under launchd) unless echo is False. The file stays open
    between lines (a drag logs every change) and is moved aside once it passes LOG_MAX_BYTES."""
    line = "%s %s\n" % (time.strftime("%H:%M:%S"), msg)
    if echo:
        sys.stderr.write(line)
    try:
        st = _logf
        if st["f"] is None or st["path"] != LOG or st["n"] >= 500:
            if st["f"] is not None:
                st["f"].close()
                st["f"] = None
            os.makedirs(os.path.dirname(LOG), exist_ok=True)
            if os.path.exists(LOG) and os.path.getsize(LOG) > LOG_MAX_BYTES:
                os.replace(LOG, LOG[:-4] + ".old.log" if LOG.endswith(".log") else LOG + ".old")
            st["f"], st["path"], st["n"] = open(LOG, "a", buffering=1), LOG, 0
        st["f"].write(line)
        st["n"] += 1
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
        self.kids = {}
        self.folded = {}           # parent macro object id -> {child tool name: child tool}, see fold_children             # id(macro tool) -> its inner tools by name, so a set doesn't re-list them
        self.last_set = 0.0        # when a change was last applied (the periodic re-scan stays out of a drag's way)
        self.scaled = {}           # timeline key -> original output settings while the bridge has scaled it, see autoscale_*
        self.scaled_loaded = False
        self.clients_empty_since = None

    # ---------------------------------------------------------------- resolve
    def versions(self):
        """Who is on the Mac side: this bridge, the installed Velaria package (VERSION beside the bridge, written by
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
        managed = self.scaled.get(self.timeline_key(tl))
        warnings = preflight(self.format, managed and (managed["setW"], managed["setH"]))
        dn = driver_notice()
        if dn:
            warnings = [dn] + warnings
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

    # ------------------------------------------------------------ output scaling for the headset
    # A square 360 timeline (5760² is common) is taller than Resolve's 7680x4320 monitor raster: without Output
    # scaling the headset gets a cropped picture, and a user who sets it by hand and forgets ships a 3840² export.
    # So while a headset is connected the bridge sets the scaling to what the headset path needs and puts the
    # original back when the last headset leaves. The originals also go to a sidecar file, so a bridge that
    # crashed or was killed mid-session restores them the next time it starts rather than leaving a timeline
    # silently scaled. A value the user changed by hand while scaled is left alone (the record is dropped).

    def timeline_key(self, tl):
        try:
            uid = tl.GetUniqueId()
        except Exception:
            uid = None
        return str(uid or tl.GetName())

    def settings_target(self, tl):
        """(GetSetting, SetSetting) of whatever holds this timeline's output size: the timeline when it uses custom
        settings, else the project."""
        custom = (tl.GetSetting("useCustomSettings") or "0") == "1"
        obj = tl if custom else self.project
        return obj.GetSetting, obj.SetSetting

    def save_sidecar(self):
        try:
            os.makedirs(os.path.dirname(SIDECAR), exist_ok=True)
            if self.scaled:
                with open(SIDECAR, "w") as f:
                    json.dump({"scaled": self.scaled}, f)   # each record names its own project
            elif os.path.exists(SIDECAR):
                os.remove(SIDECAR)
        except Exception as e:
            log("output scaling: sidecar not written: %s" % e)

    def load_sidecar(self):
        """Once per bridge start: anything a previous bridge left scaled is restored now, before any headset asks."""
        self.scaled_loaded = True
        try:
            if not os.path.exists(SIDECAR):
                return
            with open(SIDECAR) as f:
                d = json.load(f)
        except Exception as e:
            log("output scaling: sidecar unreadable, ignored: %s" % e)
            return
        self.scaled = d.get("scaled") or {}
        for rec in self.scaled.values():          # 1.0.10 sidecars named the project once, at the top
            rec.setdefault("project", d.get("project") or "")
        if self.scaled:
            log("output scaling: a previous bridge left %d timeline(s) scaled; restoring" % len(self.scaled))
            self.autoscale_restore()

    def find_timeline(self, key):
        tl = self.timeline
        if tl and self.timeline_key(tl) == key:
            return tl
        try:
            for i in range(1, (self.project.GetTimelineCount() or 0) + 1):
                t = self.project.GetTimelineByIndex(i)
                if t and self.timeline_key(t) == key:
                    return t
        except Exception:
            pass
        return None

    def autoscale_put(self, put, match, w, h):
        """Write the three output-scaling settings; True only if all three took."""
        ok = bool(put("timelineOutputResMatchTimelineRes", str(match)))
        if w and h:
            ok = bool(put("timelineOutputResolutionWidth", str(w))) and ok
            ok = bool(put("timelineOutputResolutionHeight", str(h))) and ok
        return ok

    def autoscale_apply(self):
        """Headset connected and the current timeline's output does not fit: set the scaling (once)."""
        tl = self.timeline
        if not tl:
            return
        key = self.timeline_key(tl)
        if key in self.scaled:
            # Trust, but verify: Resolve re-asserts "match timeline resolution" when the user saves the Timeline
            # Settings dialog (seen 20 Nov: a 5760 TB timeline the bridge had set to 3840 came back at 5760 while
            # the record still said scaled, and the headset got the full square). Back at the original -> set it
            # again. Any other value -> the user chose it; the record is dropped and their value stands.
            rec = self.scaled[key]
            get, put = self.settings_target(tl)
            w_, h_ = get("timelineOutputResolutionWidth"), get("timelineOutputResolutionHeight")
            if w_ is None or h_ is None:
                return   # Resolve between pages or timelines answers None for a moment: not a change of anything
            now = (str(w_), str(h_))
            match = str(get("timelineOutputResMatchTimelineRes") or "0")
            if now == (str(rec["setW"]), str(rec["setH"])) and match == "0":
                return
            if now == (str(rec["ow"]), str(rec["oh"])) or match == "1":
                ok = self.autoscale_put(put, 0, rec["setW"], rec["setH"])
                log("output scaling: '%s' had reverted to %sx%s (Resolve re-applied its settings); set %dx%d again%s"
                    % (rec["name"], now[0], now[1], rec["setW"], rec["setH"], "" if ok else " -- Resolve refused"))
                if ok:
                    self.refresh_timeline()
            else:
                log("output scaling: '%s' was changed by hand to %sx%s; leaving it" % (rec["name"], now[0], now[1]))
                del self.scaled[key]
                self.save_sidecar()
            return
        name = tl.GetName()
        project = self.project.GetName() if self.project else None
        if not name or not project:     # Resolve is between projects or timelines: a dead handle answers None
            return
        fmt = getattr(self, "format", None) or self.timeline_format(tl)
        fit = fit_size(fmt["outWidth"], fmt["outHeight"])
        if not fit:
            return
        get, put = self.settings_target(tl)
        rec = {"name": name, "project": project, "custom": fmt["custom"],
               "match": get("timelineOutputResMatchTimelineRes") or "1",
               "ow": get("timelineOutputResolutionWidth") or "", "oh": get("timelineOutputResolutionHeight") or "",
               "setW": fit[0], "setH": fit[1]}
        # the record is saved before the writes: if the bridge dies between them, the next start restores
        self.scaled[key] = rec
        self.save_sidecar()
        ok = self.autoscale_put(put, 0, fit[0], fit[1])
        if not ok:
            # one of the three writes failed: put back whatever did take, and keep nothing (review finding 7)
            self.autoscale_put(put, rec["match"], rec["ow"], rec["oh"])
            del self.scaled[key]
            self.save_sidecar()
            log("output scaling: Resolve refused to set %dx%d on '%s'; original settings put back" % (fit[0], fit[1], rec["name"]))
            return
        log("output scaling: '%s' %dx%d -> %dx%d for the headset (was match=%s %sx%s)"
            % (rec["name"], fmt["outWidth"], fmt["outHeight"], fit[0], fit[1], rec["match"], rec["ow"], rec["oh"]))
        self.refresh_timeline()

    def autoscale_restore(self, only=None, reason=None):
        """Put back every scaled timeline (or one key). A timeline whose scaling the user changed since is left as
        the user left it. A record whose restore Resolve refuses is kept and tried again next tick."""
        here = None
        try:
            here = self.project.GetName() if self.project else None
        except Exception:
            pass
        waiting = getattr(self, "scaled_waiting", None)
        if waiting is None:
            waiting = self.scaled_waiting = set()
        failed = getattr(self, "scaled_failed", None)
        if failed is None:
            failed = self.scaled_failed = set()
        for key in list(self.scaled):
            if only and key != only:
                continue
            rec = self.scaled[key]
            # A timeline in another project (the user opened one with the headset connected, then moved on) is put
            # back the next time that project is current; said once, not every tick.
            tl = self.find_timeline(key) if (not rec.get("project") or rec.get("project") == here) else None
            if not tl:
                if key not in waiting:
                    waiting.add(key)
                    log("output scaling: '%s' (project '%s') is not open; its original scaling is restored when it is"
                        % (rec["name"], rec.get("project") or "?"))
                continue
            waiting.discard(key)
            get, put = self.settings_target(tl)
            now_w, now_h = get("timelineOutputResolutionWidth"), get("timelineOutputResolutionHeight")
            if now_w is None or now_h is None:
                continue   # transient None (review 3): keep the record, try again next tick
            match = str(get("timelineOutputResMatchTimelineRes") or "0")
            if (str(now_w), str(now_h)) != (str(rec["setW"]), str(rec["setH"])) or match != "0":
                # the size or the match checkbox is not what the bridge set: the user's choice, left alone
                log("output scaling: '%s' was changed by hand (match=%s %sx%s); leaving it" % (rec["name"], match, now_w, now_h))
            else:
                if not self.autoscale_put(put, rec["match"], rec["ow"], rec["oh"]):
                    if key not in failed:
                        failed.add(key)
                        log("output scaling: Resolve refused to restore '%s' (match=%s %sx%s); trying again"
                            % (rec["name"], rec["match"], rec["ow"], rec["oh"]))
                    continue   # record kept; next tick tries again
                log("output scaling: '%s' restored (match=%s %sx%s)%s" % (rec["name"], rec["match"], rec["ow"], rec["oh"],
                                                                        " -- %s" % reason if reason else ""))
            failed.discard(key)
            del self.scaled[key]
        self.save_sidecar()
        if self.timeline:
            self.refresh_timeline()   # the format and the preflight line follow the restored settings

    def delivering(self):
        """On the Deliver page or rendering: the output scaling is the user's render size, never the headset's."""
        try:
            if self.resolve and self.resolve.GetCurrentPage() == "deliver":
                return True
            if self.project and self.project.IsRenderingInProgress():
                return True
        except Exception:
            pass
        return False

    def autoscale_tick(self, now=None):
        """Called from the main loop after each timeline refresh, with the lock held."""
        if not self.scaled_loaded:
            self.load_sidecar()
        now = time.time() if now is None else now
        connected = bool(self.clients)
        if connected:
            self.clients_empty_since = None
            if self.delivering():
                # a render must go out at the user's size: restore now, scale again when they leave the Deliver page
                if self.scaled:
                    self.autoscale_restore(reason="Deliver page / rendering")
                return
            if self.timeline:
                here = self.timeline_key(self.timeline)
                for key in list(self.scaled):     # the user moved to another timeline: put the old one back
                    if key != here:
                        self.autoscale_restore(only=key)
            self.autoscale_apply()
        elif self.scaled:
            # the headset reconnects within a few seconds all the time (app restarts, USB re-enumeration): restore
            # only once it has been gone for 5 s, so the timeline does not flap
            if getattr(self, "clients_empty_since", None) is None:
                self.clients_empty_since = now
                return
            if now - self.clients_empty_since >= RESTORE_GRACE_S:
                self.autoscale_restore()

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
                              "fusion": it.GetFusionCompCount() > 0, "enabled": self.clip_enabled(it)})
        markers = []
        for frame, m in (tl.GetMarkers() or {}).items():
            markers.append({"frame": tl.GetStartFrame() + int(frame), "name": m.get("name", ""),
                            "color": m.get("color", ""), "note": m.get("note", "")})
        markers.sort(key=lambda x: x["frame"])
        return {"type": "timeline", "name": tl.GetName(), "start": tl.GetStartFrame(), "end": tl.GetEndFrame(),
                "fps": self.fps, "drop": self.drop, "tracks": tl.GetTrackCount("video"), "clips": clips,
                "markers": markers, "format": getattr(self, "format", {}), "warnings": getattr(self, "warnings", [])}

    def signature(self):
        tl = self.timeline
        sig = [tl.GetName(), tl.GetStartFrame(), tl.GetEndFrame(), repr(getattr(self, "format", None)), tuple(getattr(self, "warnings", []))]
        for v in range(1, tl.GetTrackCount("video") + 1):
            items = tl.GetItemListInTrack("video", v) or []
            sig.append((v, len(items), items[-1].GetEnd() if items else 0, sum(1 for it in items if not self.clip_enabled(it))))
        return tuple(sig)

    @staticmethod
    def clip_enabled(it):
        """Resolve 18.5+ answers GetClipEnabled; older builds have no such call, and a clip is then taken as enabled."""
        try:
            v = it.GetClipEnabled()
            return True if v is None else bool(v)
        except Exception:
            return True

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

    # Fusion tools (besides the Velaria macros) worth driving from the headset, with the inputs they expose.
    # PanoMap: re-orient a whole sphere (fix a wonky horizon); LatLongPatcher: the nadir/zenith patch window.
    TOOL_INPUTS = {
        "PanoMap": ("Rotate.X", "Rotate.Y", "Rotate.Z"),
        "LatLongPatcher": ("Rotate.X", "Rotate.Y", "Rotate.Z", "AoV"),
    }

    # what a tool is to the headset: "card" (a Velaria macro: pickable, on the CARDS layer), "patch" (a LatLong
    # Patcher window) or "orient" (a PanoMap turning the whole sphere); patch + orient live on the PATCHES layer
    TOOL_KIND = {"PanoMap": "orient", "LatLongPatcher": "patch"}

    MACRO_PREFIXES = ("Velaria_", "Kalyx_")

    @classmethod
    def macro_kind(cls, name):
        for pre in cls.MACRO_PREFIXES:
            if name.startswith(pre + "Orient"):
                return "orient"
            if name.startswith(pre + "Trim"):
                return "trim"
            if name.startswith(pre + "Stereo"):
                return "stereo"   # Velaria Stereo 360 / 180: convergence and eye swap on the whole picture
        return "card"

    def edit_tools(self, comp):
        """The tools of a comp the headset gets controls for: every macro, plus the whitelisted stock tools."""
        out = []
        inside = set()   # tools living inside a macro (GetToolList also returns those): the macro is the control
        for _, t in (comp.GetToolList(False, "MacroOperator") or {}).items():
            # Velaria Orient 360 (split → PanoMap per eye → combine) turns the whole plate: PICTURE layer, not a card.
            # Velaria Trim (a ColorCorrector on an adjustment clip) grades the whole picture: PICTURE layer too, kind "trim".
            # Instances from before the rename (Kalyx_*) are still recognised, so old timelines keep working.
            kind = self.macro_kind(t.Name)
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
        self.kids.clear()   # the tool objects below are fetched afresh, so anything remembered about the old ones goes
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
                    q = {"id": pid, "name": a.get("INPS_Name") or pid, "kind": kind,
                         "min": a.get("INPN_MinScale", 0.0), "max": a.get("INPN_MaxScale", 1.0),
                         "value": val, "animated": bool(inp.GetKeyFrames())}
                    # the macro author's page and colour grouping: the headset's tabs and swatches follow them
                    page = a.get("INPS_ICS_ControlPage")
                    if page and page not in ("Controls", "Common"):
                        q["page"] = str(page)
                    cg = a.get("INPI_IC_ControlGroup")
                    if cg:
                        q["cgroup"] = int(cg)
                    params.append(q)
                if allowed is None:
                    params += self.virtual_params(tool, {q["id"] for q in params})
                params = self.organise(params)
                # a Canvas Control whose window shows nothing (3D scene only) has no image, border or glow to set:
                # those controls go, so a title card is not three tabs of things that do nothing (Nelson, 20 Nov)
                shows = next((q["value"] for q in params if q["id"] == "Source"), None)
                if shows is not None and int(round(float(shows))) == 2:
                    params = [q for q in params if q["id"] == "Source" or q.get("section") not in ("Image", "Border", "Glow")]
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
                                   "params": params, "source": self.comp_source(it, comp), "_tool": tool})
                self.comp_cache[key] = (it, comp, {t.Name: t for t, _, _ in tools})
            self.fold_children(groups, key)
        for g in groups:
            g["label"] = self.group_label(g)
            g.pop("_tool", None)
        order = {"card": 0, "orient": 1, "trim": 2, "stereo": 2, "patch": 3}
        groups.sort(key=lambda g: order.get(g["kind"], 3))
        return {"type": "params", "frame": self.current_frame(), "groups": groups}

    def fold_children(self, groups, key):
        """A macro whose output feeds another macro's input (a title plugged into Canvas Control's 3D Scene) is
        part of that card, not a second card (Nelson, 20 Nov: "we want to control one thing, not two things").
        Its controls join the parent's under the child's own pages; their ids carry the child's name so writes
        reach the right tool (see targets)."""
        mine = [g for g in groups if g["clip"] == key and "_tool" in g]
        by_name = {g["tool"]: g for g in mine}
        for g in mine:
            tool = g["_tool"]
            parent = None
            try:
                outs = tool.GetOutputList() or {}
                out = outs[sorted(outs)[0]] if outs else tool.FindMainOutput(1)
                for _, inp in (out.GetConnectedInputs() or {}).items():
                    t = inp.GetTool()
                    if t and t.Name in by_name and t.Name != tool.Name:
                        parent = by_name[t.Name]
                        break
            except Exception:
                continue
            if not parent:
                continue
            child = re.sub(r"_\d+$", "", tool.Name)
            for q in g["params"]:
                q = dict(q)
                q["id"] = ">%s|%s" % (tool.Name, q["id"])
                if "ids" in q:
                    q["ids"] = [">%s|%s" % (tool.Name, i) for i in q["ids"]]
                q["section"] = q.get("section") or "Look"
                parent["params"].append(q)
            parent.setdefault("children", []).append(tool.Name)
            self.folded.setdefault(id(parent["_tool"]), {})[tool.Name] = tool
            g["_folded"] = True
        groups[:] = [g for g in groups if not g.get("_folded")]

    # Words that say what a macro is, for the card's name on the strip (the operator id minus the vendor prefix)
    MACRO_WORDS = {"CanvasControl": "Window", "FactWindow": "Window", "Orient360": "Orient 360", "Orient180": "Orient 180",
                   "Trim": "Trim", "Stereo360": "Stereo 360", "Stereo180": "Stereo 180"}

    def group_label(self, g):
        """What the card is called on the strip: a title macro plugged into it names it ("Title · Editorial"), else
        what its window shows (Footage window / Graphic / 3D), else the macro's own name."""
        if g["kind"] != "card":
            name = re.sub(r"^(Velaria_|Kalyx_|Skylight_)", "", g["tool"])
            return self.MACRO_WORDS.get(re.sub(r"_\d+$", "", name), name.replace("_", " "))
        kids = g.get("children") or []
        if kids:
            k = re.sub(r"_\d+$", "", kids[0])
            k = re.sub(r"^(Velaria|Kalyx|Skylight)_?", "", k)
            k = re.sub(r"(?i)title\s*test\s*", "", k).replace("_", " ").strip(" -·")
            k = re.sub(r"(?i)^3d\s*title\s*", "", k).strip(" -·")
            return "Title · %s" % k if k else "Title"
        shows = None
        for q in g["params"]:
            if q["id"] == "Source":
                try: shows = int(round(float(q["value"])))
                except Exception: pass
        if shows == 1:
            return "Graphic"
        if shows == 2:
            return "3D scene"
        base = re.sub(r"^(Velaria_|Kalyx_|Skylight_)", "", re.sub(r"_\d+$", "", g["tool"]))
        return "Footage window" if base in self.MACRO_WORDS and self.MACRO_WORDS[base] == "Window" else base.replace("_", " ")

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
        # Listing them costs one round trip to Resolve per inner tool, and a drag on Scale or a card rotation asked
        # for the list on every step. The answer is kept while the tool objects are (see macro_params).
        hit = self.kids.get(id(tool))
        if hit is not None and hit[0] is tool:
            return hit[1]
        try:
            kids = {re.sub(r"_\d+$", "", c.Name): c for c in (tool.GetChildrenList() or {}).values()}
        except Exception:
            return {}
        if kids:
            self.kids[id(tool)] = (tool, kids)
        return kids

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
        """(tool, input) pairs that a control writes to: the macro input itself, inner tools for a virtual one, or a
        child macro's input for a folded control (">Child|Input")."""
        if pid.startswith(">"):
            name, _, inner = pid[1:].partition("|")
            child = self.folded.get(id(tool), {}).get(name)
            return self.targets(child, inner) if child else []
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
        # Velaria Trim
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
        "Convergence": "Stereo", "SwapEyes": "Stereo",
    }

    PLACEMENT = ("yaw", "pitch", "roll", "pan", "tilt", "distance", "depth", "rotate", "rotation", "spin",
                 "position", "size", "scale", "offset")
    # a macro author's page names -> what the tab says (dev names from the title tests included)
    PAGE_NAMES = {"Slab": "Plate", "Light": "Lighting", "Lights": "Lighting", "Stereo / Output": "Stereo", "Settings": "Output",
                  "Frame": "Border", "Window": "Image", "Env": "Reflections", "Environment": "Reflections", "Text": "Text",
                  "3D Model": "Model", "Animation": "Animation"}
    # pages whose name repeated at the front of a control is noise ("Slab Width" on the Slab page); not Text
    STRIP_PAGE_WORD = {"Slab", "Plate", "Frame", "Window", "Glow", "Light", "Lights", "Border", "Image"}
    # words in a control's name that read as node names, not as what the control does
    WORD_NAMES = {"Slab": "Plate", "Key": "Key light", "Fill": "Fill light", "Env": "Reflections", "Spec": "Specular"}

    def tidy_name(self, name, page):
        """'Slab Width' on the Slab page -> 'Width'; 'Key Light Yaw (deg, + = from the left)' -> 'Key light yaw';
        parentheticals go when the name is long enough to need the room."""
        n = name.strip()
        if page in self.STRIP_PAGE_WORD:
            for w in (page, self.PAGE_NAMES.get(page, "")):
                if w and n.lower().startswith(w.lower() + " ") and len(n) > len(w) + 3:
                    n = n[len(w) + 1:]
        if len(n) > 22:
            n = re.sub(r"\s*\([^)]*\)\s*$", "", n).strip() or n
        parts = n.split(" ")
        if parts and parts[0] in self.WORD_NAMES and len(parts) > 1:
            full = self.WORD_NAMES[parts[0]]
            if not (parts[1].lower() == full.split(" ")[-1].lower()):
                parts[0] = full
                n = " ".join(parts)
        # sentence case: lower-case the first letter of each later word unless the word is an acronym or a unit
        words = n.split(" ")
        for i in range(1, len(words)):
            w = words[i]
            if len(w) > 1 and w[0].isupper() and w[1:].islower() and w not in ("L/R", "X", "Y", "Z"):
                words[i] = w[0].lower() + w[1:]
        n = " ".join(words)
        return n

    def organise(self, params):
        """Merges Fusion colour triplets (X, XG, XB) into one colour control and files every control in a section."""
        ids = {q["id"]: q for q in params}
        # Fusion's ControlGroup ties the R/G/B(/A) inputs of one colour together; three or four numbers in a group
        # are one swatch (the title macros publish FaceR/FaceG/FaceB that way and the headset showed "Green, Blue")
        cgroups = {}
        for q in params:
            if q.get("cgroup") and q["kind"] == "number":
                cgroups.setdefault(q["cgroup"], []).append(q)
        in_cgroup = set()
        for cg, members in cgroups.items():
            if 3 <= len(members) <= 4:
                for m in members[1:]:
                    in_cgroup.add(m["id"])
        merged = []
        for q in params:
            pid = q["id"]
            if pid in in_cgroup:
                continue
            cg = q.get("cgroup")
            if cg and cg in cgroups and 3 <= len(cgroups[cg]) <= 4 and cgroups[cg][0] is q:
                mem = cgroups[cg][:3]
                name = q["name"]
                for suffix in (" \u00b7 R", " Red", " R", " Colour Red", " Color Red"):
                    if name.endswith(suffix):
                        name = name[:-len(suffix)]
                q = dict(q, kind="colour", name=name, ids=[m["id"] for m in mem],
                         value=[m["value"] for m in mem], min=0.0, max=1.0)
                merged.append(q)
                continue
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
            # "Mono (0) / Stereo (1)" and "over/under stereo (1) or mono (0)" style inputs are switches, not sliders
            if re.search(r"\(0\).*\(1\)|\(1\).*\(0\)", name) and float(q["min"]) == 0.0 and float(q["max"]) == 1.0:
                q["kind"] = "bool"
                nm = re.sub(r"\s*\(\d\)", "", name)
                nm = re.sub(r"\s+or\s+\w+$", "", nm)          # "... stereo or mono" -> "... stereo"
                q["name"] = nm.replace(" / ", " / ").strip()
                if name.lower().startswith("mono") and "stereo" in name.lower():
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
        # friendly names and tab names; the macro's own page wins when it has one
        for q in merged:
            if q["id"] in self.SECTION_BY_ID:
                q["section"] = self.SECTION_BY_ID[q["id"]]
            elif q.get("page"):
                q["section"] = self.PAGE_NAMES.get(q["page"], self.SECTION_NAMES.get(q["page"], q["page"]))
            else:
                q["section"] = self.SECTION_NAMES.get(q["section"], q["section"])
            if q["id"] in self.DISPLAY_NAMES:
                q["name"] = self.DISPLAY_NAMES[q["id"]]
            else:
                q["name"] = self.tidy_name(q["name"], q.get("page") or "")
            q.pop("page", None)
            q.pop("cgroup", None)
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
        if batch:
            self.last_set = time.time()
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
                    log("set %s.%s = %r (%.0f ms)" % (toolname, pid, value, (time.time() - t0) * 1000), echo=False)
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
        elif cmd == "hide":
            # {"cmd":"hide","clips":[keys],"on":1} disables those clips in Resolve (the Edit page's Enable Clip, D key);
            # on=0 enables them again. Resolve re-renders once per clip, like any timeline change.
            keys = msg.get("clips") or ([msg["clip"]] if msg.get("clip") else [])
            n = self.set_hidden(keys, bool(msg.get("on", 1)))
            self.broadcast(self.timeline_snapshot())
            if n:
                self.broadcast({"type": "notice", "msg": ("Hidden %d" if msg.get("on", 1) else "Showing %d") % n + (" clip" if n == 1 else " clips")})
        elif cmd == "marker":
            # add: a review note at the playhead, carrying where the reviewer was looking (Canvas Control yaw / pitch)
            # remove: the marker under the playhead; jump: previous / next marker
            action = msg.get("action", "add")
            if action == "jump":
                self.jump_marker(int(msg.get("dir", 1)))
            else:
                text = self.marker(msg) if action == "add" else self.remove_marker()
                self.broadcast(self.timeline_snapshot())
                if text:
                    self.broadcast({"type": "notice", "msg": text})
        elif cmd == "still":
            self.broadcast({"type": "notice", "msg": self.grab_still()})
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

    def set_hidden(self, keys, hidden):
        """Enable or disable the timeline items with these clip keys (track:start). Returns how many changed."""
        want = set(keys)
        n = 0
        tl = self.timeline
        with self.lock:
            for v in range(1, tl.GetTrackCount("video") + 1):
                for it in tl.GetItemListInTrack("video", v) or []:
                    try:
                        key = self.clip_key(v, it)
                    except TransientError:
                        continue
                    if key not in want:
                        continue
                    if self.clip_enabled(it) == (not hidden):
                        continue
                    try:
                        if it.SetClipEnabled(not hidden):
                            n += 1
                        else:
                            log("hide: SetClipEnabled refused for %s" % key)
                    except Exception as e:
                        log("hide: %s: %s" % (key, e))
            self.tl_signature = None   # force the next re-scan to broadcast
        return n

    # ---------------------------------------------------------------- stills (frame grabs from the headset)
    STILLS_FOLDER = "Velaria Stills"

    def stills_dir(self):
        """Beside the media of the clip under the playhead (Nelson, 18 Nov: 'next to the project's media keeps it
        organised'); ~/Pictures/Velaria Stills when there is no clip or that place cannot be written."""
        folder = None
        try:
            it = self.timeline.GetCurrentVideoItem()
            mp = it.GetMediaPoolItem() if it else None
            path = mp.GetClipProperty("File Path") if mp else ""
            if path:
                folder = os.path.join(os.path.dirname(path), self.STILLS_FOLDER)
        except Exception as e:
            log("still: clip path: %s" % e)
        for cand in ([folder] if folder else []) + [os.path.expanduser("~/Pictures/" + self.STILLS_FOLDER)]:
            try:
                os.makedirs(cand, exist_ok=True)
                if os.access(cand, os.W_OK):
                    return cand
            except OSError:
                continue
        return None

    def grab_still(self):
        # Project.ExportCurrentFrameAsStill writes the current frame at the timeline's output size, stays on the
        # current page (0.1-0.3 s on the development Mac, 18 Nov) and picks the format from the extension. GrabStill, the gallery
        # route, flipped Resolve to the Color page, which is why it was dropped.
        page = None
        try:
            page = self.resolve.GetCurrentPage()
        except Exception:
            pass
        folder = self.stills_dir()
        if not folder:
            return "Nowhere to save a still (no writable folder)"
        tc = frame_to_tc(self.current_frame(), self.fps, self.drop).replace(":", "-").replace(";", "-")
        safe = "".join(ch if ch.isalnum() or ch in " -_." else "_" for ch in self.timeline.GetName()).strip() or "timeline"
        path = os.path.join(folder, "%s %s.jpg" % (safe, tc))
        n = 2
        while os.path.exists(path):
            path = os.path.join(folder, "%s %s (%d).jpg" % (safe, tc, n))
            n += 1
        try:
            ok = bool(self.project.ExportCurrentFrameAsStill(path))
        except Exception as e:
            log("still: %s" % e)
            ok = False
        try:
            if page and self.resolve.GetCurrentPage() != page:
                self.resolve.OpenPage(page)
        except Exception:
            pass
        if not ok:
            return "Resolve could not export this frame"
        log("still -> %s" % path)
        home = os.path.expanduser("~")
        shown = path[len(home) + 1:] if path.startswith(home + "/") else path
        return "Still saved: " + shown

    # ---------------------------------------------------------------- markers (review notes from the headset)
    MARKER_COLOURS = ("Blue", "Cyan", "Green", "Yellow", "Red", "Pink", "Purple", "Fuchsia", "Rose", "Lavender", "Sky", "Mint", "Lemon", "Sand", "Cocoa", "Cream")

    def marker(self, msg):
        tl = self.timeline
        frame = self.current_frame()
        rel = frame - tl.GetStartFrame()
        colour = msg.get("color") or "Cyan"
        if colour not in self.MARKER_COLOURS:
            colour = "Cyan"
        name = (msg.get("name") or "Headset note")[:80]
        note = msg.get("note") or ""
        custom = ""
        if "pan" in msg and "tilt" in msg:
            try:
                pan, tilt = float(msg["pan"]), float(msg["tilt"])
                gaze = "Looking at yaw %+.0f\u00b0, pitch %+.0f\u00b0 (Canvas Control Yaw / Pitch)" % (pan, tilt)
                note = (note + "\n" if note else "") + gaze
                custom = "velaria:pan=%.1f;tilt=%.1f" % (pan, tilt)
            except (TypeError, ValueError):
                pass
        if msg.get("card"):
            note += "\nPicked: %s" % msg["card"]
        existing = tl.GetMarkers() or {}
        if rel in existing or str(rel) in existing:
            # Resolve keeps one marker per frame: refresh the note instead of failing quietly
            tl.DeleteMarkerAtFrame(rel)
        ok = tl.AddMarker(rel, colour, name, note, 1, custom)
        log("marker %s at %d -> %s" % (colour, frame, ok))
        return ("%s marker at %s" % (colour, frame_to_tc(frame, self.fps, self.drop))) if ok else "Resolve refused the marker"

    def remove_marker(self):
        tl = self.timeline
        frame = self.current_frame()
        rel = frame - tl.GetStartFrame()
        ok = tl.DeleteMarkerAtFrame(rel)
        return ("Marker removed at %s" % frame_to_tc(frame, self.fps, self.drop)) if ok else "No marker under the playhead"

    def jump_marker(self, direction):
        tl = self.timeline
        frame = self.current_frame()
        frames = sorted(tl.GetStartFrame() + int(f) for f in (tl.GetMarkers() or {}).keys())
        if direction > 0:
            nxt = [f for f in frames if f > frame]
            if nxt:
                self.seek(nxt[0])
        else:
            prv = [f for f in frames if f < frame]
            if prv:
                self.seek(prv[-1])

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

    def read_commands(self, client, buf):
        """One recv: returns (commands parsed from complete lines, the unfinished tail, False once the peer is gone
        or is sending something that is not this protocol)."""
        data = client.recv(65536)
        if not data:
            return [], buf, False
        buf += data
        msgs = []
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                msg = json.loads(line)
            except (ValueError, RecursionError):   # RecursionError: a line of 100,000 "[" is not a ValueError
                continue
            if isinstance(msg, dict):
                msgs.append(msg)
        if len(buf) > MAX_LINE:
            log("dropping a client that sent %d bytes without a line end" % len(buf))
            return msgs, b"", False
        return msgs, buf, True

    def dispatch(self, client, msgs):
        """Runs commands in order. Consecutive "set" commands are applied as one batch under a comp lock, so a
        change the headset sends as several inputs (pan + tilt from a grab, the three channels of a colour, the
        rows queued while Resolve was rendering) costs one Resolve render, not one per input."""
        i = 0
        while i < len(msgs):
            msg = msgs[i]
            i += 1
            with self.lock:
                if self.timeline is None:
                    self.send(client, {"type": "error", "msg": "Resolve not available"})
                    continue
                try:
                    if msg.get("cmd") == "set":
                        run = [msg]
                        while i < len(msgs) and msgs[i].get("cmd") == "set":
                            run.append(msgs[i])
                            i += 1
                        for m in run:
                            msg = m   # named in the log if this one is malformed
                            self.pending[(m["clip"], m["tool"], m["input"])] = m["value"]
                        self.apply_pending()
                    else:
                        self.handle(client, msg)
                except Exception as e:
                    log("command %s failed: %s\n%s" % (str(msg)[:300], e, traceback.format_exc()))
                    self.send(client, {"type": "error", "msg": str(e)[:120]})

    def client_loop(self, client, peer):
        log("headset connected from %s:%d" % peer)
        buf = b""
        client.settimeout(None)
        try:
            alive = True
            while alive:
                msgs, buf, alive = self.read_commands(client, buf)
                # The inputs of one change leave the headset as separate lines microseconds apart and can land in
                # separate reads. Give the rest a moment to arrive before touching Resolve: 2 ms against a render
                # that takes hundreds.
                tries = 0
                while alive and msgs and msgs[-1].get("cmd") == "set" and tries < 8:
                    tries += 1
                    if not select.select([client], [], [], SET_GATHER_S)[0]:
                        break
                    more, buf, alive = self.read_commands(client, buf)
                    msgs += more
                if msgs:
                    self.dispatch(client, msgs)
        except (OSError, ValueError):
            pass
        finally:
            # whatever ended this thread, give the connection's slot back: a thread that died without doing so left
            # its client in the list for good, and MAX_CLIENTS of those shut every headset out until a restart
            log("headset %s:%d disconnected" % peer)
            self.drop_client(client)

    def accept_loop(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        addr = bind_address()
        # The port can be taken (an older bridge still exiting, another copy started by hand). This runs on a
        # daemon thread: an exception here used to end the thread and leave a bridge that ran but never listened,
        # which launchd does not restart. Keep trying instead.
        warned = 0.0
        while True:
            try:
                srv.bind((addr, PORT))
                break
            except OSError as e:
                if time.time() - warned > 30:
                    warned = time.time()
                    log("cannot listen on TCP %d (%s); retrying every 2 s" % (PORT, e))
                time.sleep(2)
        srv.listen(4)
        log("bridge listening on TCP %d (%s)" % (PORT, "this Mac only: USB" if addr.startswith("127.") else addr))
        while True:
            try:
                c, peer = srv.accept()
            except OSError as e:
                log("accept failed: %s" % e)
                time.sleep(1)
                continue
            with self.lock:
                full = len(self.clients) >= MAX_CLIENTS
                if not full:
                    self.clients.append(c)
            if full:   # every connection is a thread that can drive Resolve; a headset needs one
                c.close()
                continue
            c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
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
                    # The re-scan below walks every clip (about 100 ms on a 180-clip timeline) with the lock held,
                    # and a change from the headset waits for that lock. While changes are arriving, hold it off.
                    dragging = now - self.last_set < DRAG_QUIET_S
                    if now >= next_sig and not dragging:
                        next_sig = now + 2.0
                        self.refresh_timeline()
                        self.autoscale_tick()
                        sig = self.signature()
                        self.rebuild_index()
                        if sig != self.tl_signature:
                            self.tl_signature = sig
                            self.comp_cache.clear()
                            self.animated.clear()
                            self.kids.clear()
                            if self.clients:
                                self.broadcast(self.timeline_snapshot())
                    frame = self.current_frame()
                    if frame != self.last_frame:
                        self.last_frame = frame
                        if self.clients:
                            self.broadcast(self.state())
                    # when the playhead lands on a different set of clips, push their controls (at most 4x/s)
                    if self.clients and now >= next_params and not dragging:
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
