#!/usr/bin/env python3
"""Kalyx Trim: a headset trim pass on an adjustment clip — one ColorCorrector with the controls a trim needs,
published on a Trim page. Temperature / tint are approximated with per-channel gain. (Design call: roadmap,
"Headset trim pass"; input IDs verified with GetInputList on Resolve 21.1, 1 Nov 2026.)"""
import settingwrite as sw

def inp(src, source, name, default, lo, hi, page="Trim"):
    return {"__type": "InstanceInput", "SourceOp": src, "Source": source, "Name": name, "Default": default,
            "MinScale": lo, "MaxScale": hi, "Page": page}

def uc(name, default, lo, hi):
    return {"LINKID_DataType": "Number", "INPID_InputControl": "SliderControl", "INP_Default": default,
            "INP_MinScale": lo, "INP_MaxScale": hi, "INP_MinAllowed": lo, "INP_MaxAllowed": hi,
            "LINKS_Name": name, "ICS_ControlPage": "Trim"}

inputs = {"__ordered": True}
inputs["MainInput1"] = {"__type": "InstanceInput", "SourceOp": "Trim_CC", "Source": "Input", "Name": "Picture"}
inputs["Exposure"] = inp("Trim_CC", "MasterRGBBrightness", "Exposure (offset)", 0, -0.5, 0.5)
inputs["Lift"] = inp("Trim_CC", "MasterRGBLift", "Lift", 0, -0.25, 0.25)
inputs["Gamma"] = inp("Trim_CC", "MasterRGBGamma", "Gamma", 1, 0.5, 2)
inputs["Gain"] = inp("Trim_CC", "MasterRGBGain", "Gain", 1, 0, 2)
inputs["Contrast"] = inp("Trim_CC", "MasterRGBContrast", "Contrast", 1, 0.5, 2)
inputs["Saturation"] = inp("Trim_CC", "Saturation1", "Saturation", 1, 0, 2)
inputs["Temperature"] = inp("Kalyx_TrimControls", "Temperature", "Temperature (blue - amber)", 0, -1, 1)
inputs["Tint"] = inp("Kalyx_TrimControls", "Tint", "Tint (green - magenta)", 0, -1, 1)
inputs["Amount"] = inp("Trim_CC", "Blend", "Amount (0 = off)", 1, 0, 1)   # published as Amount: the bridge hides every "Blend"

tools = {"__ordered": True}
tools["Kalyx_TrimControls"] = {
    "__type": "PipeRouter", "CtrlWZoom": False,
    "Inputs": {"Temperature": {"__type": "Input", "Value": 0}, "Tint": {"__type": "Input", "Value": 0}},
    "ViewInfo": {"__type": "PipeRouterInfo", "Pos": [-220, -60]},
    "UserControls": {"__ordered": True, "Temperature": uc("Temperature (blue - amber)", 0, -1, 1), "Tint": uc("Tint (green - magenta)", 0, -1, 1)},
}
tools["Trim_CC"] = {
    "__type": "ColorCorrector",
    "Inputs": {
        "MasterRGBBrightness": {"__type": "Input", "Value": 0},
        "MasterRGBLift": {"__type": "Input", "Value": 0},
        "MasterRGBGamma": {"__type": "Input", "Value": 1},
        "MasterRGBGain": {"__type": "Input", "Value": 1},
        "MasterRGBContrast": {"__type": "Input", "Value": 1},
        "Saturation1": {"__type": "Input", "Value": 1},
        # warm = more red, less blue; magenta = less green, a touch more red and blue
        "MasterRedGain": {"__type": "Input", "Value": 1, "Expression": "1 + 0.25*Kalyx_TrimControls.Temperature + 0.08*Kalyx_TrimControls.Tint"},
        "MasterGreenGain": {"__type": "Input", "Value": 1, "Expression": "1 - 0.16*Kalyx_TrimControls.Tint"},
        "MasterBlueGain": {"__type": "Input", "Value": 1, "Expression": "1 - 0.25*Kalyx_TrimControls.Temperature + 0.08*Kalyx_TrimControls.Tint"},
    },
    "ViewInfo": {"__type": "OperatorInfo", "Pos": [0, 0]},
}
macro = {"__type": "MacroOperator", "Inputs": inputs,
         "Outputs": {"MainOutput1": {"__type": "InstanceOutput", "SourceOp": "Trim_CC", "Source": "Output", "Name": "Picture"}},
         "Tools": tools, "ViewInfo": {"__type": "GroupInfo", "Pos": [0, 0]}}
sw.write_setting({"Tools": {"__ordered": True, "Kalyx_Trim": macro}}, "Kalyx Trim.setting")
print("wrote Kalyx Trim.setting")
