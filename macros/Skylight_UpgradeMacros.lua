-- Skylight_UpgradeMacros.lua  (Fusion comp script: Workspace > Scripts > Comp > Skylight_UpgradeMacros)
--
-- Replaces every Kalyx_CanvasControl (or older Skylight_FactWindow) macro instance in the current comp with
-- the macro as it is now in Macros:/Kalyx Canvas Control.setting, keeping every control value, animation and
-- connection. One undo step. Use it after the macro gains new controls (Object Tilt / Turn / Spin, Crop) so old
-- clips get them too.

local MACRO_NAMES = { "Kalyx_CanvasControl", "Skylight_FactWindow" }
local MACRO_NAME = MACRO_NAMES[1]
local MACRO_FILE = "Macros:/Kalyx Canvas Control.setting"

local function isTarget(name)
    for _, m in ipairs(MACRO_NAMES) do
        if name == m or name:match("^" .. m .. "%d*$") or name:match("^" .. m .. "_%d+$") then return true end
    end
    return false
end

local function names(list)
    local out = {}
    for _, t in pairs(list) do out[t.Name] = true end
    return out
end

local path = comp:MapPath(MACRO_FILE)
local setting = bmd.readfile(path)
if not setting then
    print("[Skylight] can't read " .. path)
    return
end

local flow = comp.CurrentFrame.FlowView
local targets = {}
for _, t in pairs(comp:GetToolList(false, "MacroOperator")) do
    if isTarget(t.Name) then targets[#targets + 1] = t end
end
if #targets == 0 then
    print("[Skylight] no " .. MACRO_NAME .. " in this comp")
    return
end

comp:Lock()
comp:StartUndo("Upgrade Skylight macros")
local upgraded = 0
for _, old in ipairs(targets) do
    local oldName = old.Name
    local px, py = flow:GetPos(old)
    local before = names(comp:GetToolList(false))
    comp:Paste(setting)
    local new
    for _, t in pairs(comp:GetToolList(false, "MacroOperator")) do
        if not before[t.Name] then new = t end
    end
    if not new then
        print("[Skylight] paste failed for " .. oldName)
    else
        -- inputs: connections (including animation splines) or plain values
        for _, inp in pairs(old:GetInputList()) do
            local id = inp:GetAttrs().INPS_ID
            local target = new[id]
            if target then
                local src = inp:GetConnectedOutput()
                if src then
                    target:ConnectTo(src)
                else
                    local v = inp[TIME_UNDEFINED]
                    if v ~= nil then target[TIME_UNDEFINED] = v end
                end
            end
        end
        -- outputs: whatever was fed by the old macro now takes the new one
        for _, out in pairs(old:GetOutputList()) do
            local id = out:GetAttrs().OUTS_ID
            local nout = new[id]
            if nout then
                for _, dst in pairs(out:GetConnectedInputs() or {}) do dst:ConnectTo(nout) end
            end
        end
        old:Delete()
        new:SetAttrs({ TOOLS_Name = oldName })
        if px and py then flow:SetPos(new, px, py) end
        upgraded = upgraded + 1
    end
end
comp:EndUndo(true)
comp:Unlock()
print(string.format("[Skylight] upgraded %d %s instance(s)", upgraded, MACRO_NAME))
