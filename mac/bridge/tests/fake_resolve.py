"""A stand-in for DaVinciResolveScript with just the surface vrlink_bridge.py uses.

One timeline, a few clips, one of which carries a comp with a Skylight_FactWindow macro (published inputs plus the
inner Left/Right_Window_Plane tools the bridge reaches for Scale and the card rotations). Install it with
`sys.modules["DaVinciResolveScript"] = fake_resolve` before importing the bridge.
"""
import re


class Input:
    def __init__(self, tool, pid, name, dtype="Number", ctl="SliderControl", lo=0.0, hi=1.0):
        self.tool, self.pid, self.name, self.dtype, self.ctl, self.lo, self.hi = tool, pid, name, dtype, ctl, lo, hi
        self.keys = {}

    def GetAttrs(self):
        return {"INPS_ID": self.pid, "INPS_Name": self.name, "INPS_DataType": self.dtype,
                "INPID_InputControl": self.ctl, "INPN_MinScale": self.lo, "INPN_MaxScale": self.hi}

    def GetKeyFrames(self):
        return dict(self.keys)


class Tool:
    def __init__(self, name, regid, inputs, children=None):
        self.Name, self.regid = name, regid
        self.values = {}
        self.inputs = {}
        for spec in inputs:
            pid = spec[0]
            self.inputs[pid] = Input(self, *spec[:6])
            self.values[pid] = spec[6] if len(spec) > 6 else 0.0
        self.children = children or []
        self.modifiers = set()
        self.set_log = []

    def GetAttrs(self):
        return {"TOOLS_RegID": self.regid, "TOOLS_Clip_Name": getattr(self, "clip_name", None)}

    def GetInputList(self):
        return {i + 1: inp for i, inp in enumerate(self.inputs.values())}

    def GetInput(self, pid, time=None):
        return self.values.get(pid)

    def SetInput(self, pid, value, time=None):
        if pid not in self.values:
            raise RuntimeError("no input " + pid)
        self.values[pid] = value
        self.set_log.append((pid, value, time))
        if pid in self.modifiers and time is not None:
            self.inputs[pid].keys[time] = value

    def AddModifier(self, pid, kind):
        self.modifiers.add(pid)
        return True

    def GetChildrenList(self):
        return {i + 1: c for i, c in enumerate(self.children)}


def fact_window(name="Skylight_FactWindow"):
    left = Tool("Left_Window_Plane", "ImagePlane3D", [
        ("Transform3DOp.Scale.X", "Scale X", "Number", "SliderControl", 0.0, 10.0, 1.0),
        ("Transform3DOp.ScaleLock", "Lock", "Number", "CheckboxControl", 0.0, 1.0, 1.0),
        ("Transform3DOp.Rotate.X", "RX", "Number", "ScrewControl", -180.0, 180.0, 0.0),
        ("Transform3DOp.Rotate.Y", "RY", "Number", "ScrewControl", -180.0, 180.0, 0.0),
        ("Transform3DOp.Rotate.Z", "RZ", "Number", "ScrewControl", -180.0, 180.0, 0.0)])
    right = Tool("Right_Window_Plane", "ImagePlane3D", [
        ("Transform3DOp.Rotate.X", "RX", "Number", "ScrewControl", -180.0, 180.0, 0.0),
        ("Transform3DOp.Rotate.Y", "RY", "Number", "ScrewControl", -180.0, 180.0, 0.0),
        ("Transform3DOp.Rotate.Z", "RZ", "Number", "ScrewControl", -180.0, 180.0, 0.0)])
    media = Tool("MediaIn2", "MediaIn", [])
    media.clip_name = "Shot L.mov"
    media2 = Tool("MediaIn3", "MediaIn", [])
    media2.clip_name = "Shot R.mov"
    macro = Tool(name, "MacroOperator", [
        ("MainInput1", "Left / Mono Footage", "Image", "", 0.0, 1.0),
        ("FrameColour", "Frame Colour", "Number", "ColorControl", 0.0, 1.0, 1.0),
        ("FrameColourG", "Top Left Green", "Number", "ColorControl", 0.0, 1.0, 1.0),
        ("FrameColourB", "Top Left Blue", "Number", "ColorControl", 0.0, 1.0, 1.0),
        ("FrameThickness", "Frame Thickness (0 = none)", "Number", "ColorControl", 0.0, 0.08, 0.012),
        ("WindowSize", "Window Size (smaller = more glow room)", "Number", "SliderControl", 0.3, 1.0, 0.8),
        ("CornerRadius", "Corner Radius", "Number", "SliderControl", 0.0, 0.5, 0.06),
        ("GlowAmount", "Glow Amount (0 = off)", "Number", "SliderControl", 0.0, 1.0, 0.7),
        ("Yaw", "Yaw (Pan)", "Number", "ScrewControl", -180.0, 180.0, 20.0),
        ("Pitch", "Pitch (Tilt)", "Number", "ScrewControl", -90.0, 90.0, -5.0),
        ("Roll", "Roll", "Number", "ScrewControl", -180.0, 180.0, 0.0),
        ("Distance", "Distance (-Z = forward)", "Number", "ScrewControl", -20.0, -0.5, -0.2),
        ("Mode", "Mono (0) / Stereo (1)", "Number", "ColorControl", 0.0, 1.0, 1.0),
        ("WindowDepth", "Window Depth in Z (+ = toward viewer)", "Number", "ColorControl", 0.0, 0.02, 0.01),
        ("Comments", "Comments", "Text", "TextEditControl", 0.0, 1.0, ""),
    ], children=[left, right, media, media2])
    return macro, [macro, left, right, media, media2]


class Comp:
    def __init__(self, tools, global_start=0.0):
        self.tools = tools
        self.current_time = global_start + 10
        self.global_start = global_start
        self.locks = 0

    def GetToolList(self, selected=False, kind=None):
        out = [t for t in self.tools if kind is None or t.regid == kind]
        return {i + 1: t for i, t in enumerate(out)}

    def GetAttrs(self):
        return {"COMPN_GlobalStart": self.global_start, "COMPN_CurrentTime": self.current_time}

    def Lock(self):
        self.locks += 1

    def Unlock(self):
        self.locks -= 1


class Item:
    def __init__(self, name, start, end, comp=None, color="Blue"):
        self.name, self.start, self.end, self.comp, self.color = name, start, end, comp, color

    def GetName(self): return self.name
    def GetStart(self): return self.start
    def GetEnd(self): return self.end
    def GetDuration(self): return self.end - self.start
    def GetClipColor(self): return self.color
    def GetFusionCompCount(self): return 1 if self.comp else 0
    def GetFusionCompByIndex(self, i): return self.comp
    def GetFusionCompNameList(self): return ["Composition 1"] if self.comp else []
    def GetMediaPoolItem(self): return None


def pano_map(name="PanoMap1"):
    return Tool(name, "PanoMap", [
        ("Rotate.X", "Rotate X", "Number", "ScrewControl", -180.0, 180.0, 0.0),
        ("Rotate.Y", "Rotate Y", "Number", "ScrewControl", -180.0, 180.0, 12.0),
        ("Rotate.Z", "Rotate Z", "Number", "ScrewControl", -180.0, 180.0, -3.0),
        ("Rotate.RotOrder", "Rotation Order", "FuID", "MultiButtonIDControl", 0.0, 1.0, "XYZ"),
        ("Comments", "Comments", "Text", "TextEditControl", 0.0, 1.0, ""),
    ])


class Timeline:
    def __init__(self):
        self.macro, tools = fact_window()
        self.comp = Comp(tools, global_start=100.0)
        self.pano = pano_map()
        self.plate_comp = Comp([Tool("MediaIn1", "MediaIn", []), self.pano], global_start=0.0)
        self.tracks = {
            1: [Item("plate.braw", 108000, 108600, self.plate_comp), Item("plate2.braw", 108600, 109200)],
            2: [Item("Cross Dissolve", 108590, 108610), Item("Card", 108300, 108500, self.comp, "Pink")],
        }
        self.frame = 108350
        self.page_fusion = False
        self.markers = {120: {"name": "m1", "color": "Red"}}

    def GetName(self): return "Test timeline"
    def GetStartFrame(self): return 108000
    def GetEndFrame(self): return 109200
    def GetTrackCount(self, kind): return 2
    def GetItemListInTrack(self, kind, v): return list(self.tracks.get(v, []))
    def GetSetting(self, k): return "30" if k == "timelineFrameRate" else "0"
    def GetMarkers(self): return dict(self.markers)

    def GetCurrentTimecode(self):
        if self.page_fusion:
            return None
        f = self.frame
        return "%02d:%02d:%02d:%02d" % (f // 108000, (f // 1800) % 60, (f // 30) % 60, f % 30)

    def SetCurrentTimecode(self, tc):
        h, m, s, f = [int(x) for x in re.split("[:;]", tc)]
        self.frame = ((h * 60 + m) * 60 + s) * 30 + f
        return True

    def GetCurrentVideoItem(self):
        for v in (2, 1):
            for it in self.tracks[v]:
                if it.GetStart() <= self.frame < it.GetEnd():
                    return it
        return None


class Project:
    def __init__(self):
        self.timeline = Timeline()

    def GetCurrentTimeline(self): return self.timeline
    def GetName(self): return "Test project"
    def GetSetting(self, k): return "30"


class ProjectManager:
    def __init__(self):
        self.project = Project()

    def GetCurrentProject(self): return self.project


class Resolve:
    def __init__(self):
        self.pm = ProjectManager()

    def GetProjectManager(self): return self.pm
    def GetCurrentPage(self): return "edit"


def scriptapp(name):
    """A fresh Resolve per connect, so tests never share state."""
    return Resolve()
