-- Skylight_UpgradeMacros.lua  (Fusion comp script: Workspace > Scripts > Comp > Skylight_UpgradeMacros)
--
-- Replaces every Velaria macro instance in the current comp — and every older Kalyx_* / Skylight_FactWindow
-- instance from before the rename — with the macro as it is now in Macros:/, keeping every control value,
-- animation and connection, and the instance's name. One undo step. Use it after a macro gains new controls,
-- and once after upgrading from Kalyx 1.0.x to Velaria (the macro IDs changed).

local FAMILIES = {
    { file = "Macros:/Velaria Canvas Control.setting", names = { "Velaria_CanvasControl", "Kalyx_CanvasControl", "Skylight_FactWindow" } },
    { file = "Macros:/Velaria Orient 360.setting",     names = { "Velaria_Orient360", "Kalyx_Orient360" } },
    { file = "Macros:/Velaria Trim.setting",           names = { "Velaria_Trim", "Kalyx_Trim" } },
}

local function matches(name, m)
    return name == m or name:match("^" .. m .. "%d+$") or name:match("^" .. m .. "_%d+$")
end

local function familyOf(name)
    for _, fam in ipairs(FAMILIES) do
        for _, m in ipairs(fam.names) do
            if matches(name, m) then return fam end
        end
    end
    return nil
end

local function names(list)
    local out = {}
    for _, t in pairs(list) do out[t.Name] = true end
    return out
end

local settings = {}
local function settingFor(fam)
    if settings[fam.file] == nil then
        local path = comp:MapPath(fam.file)
        settings[fam.file] = bmd.readfile(path) or false
        if not settings[fam.file] then print("[Skylight] can't read " .. path) end
    end
    return settings[fam.file]
end

local flow = comp.CurrentFrame.FlowView
local targets = {}
for _, t in pairs(comp:GetToolList(false, "MacroOperator")) do
    local fam = familyOf(t.Name)
    if fam then targets[#targets + 1] = { tool = t, fam = fam } end
end
if #targets == 0 then
    print("[Skylight] no Velaria (or Kalyx / Skylight_FactWindow) macros in this comp")
    return
end

comp:Lock()
comp:StartUndo("Upgrade Velaria macros")
local upgraded, skipped = 0, 0
for _, item in ipairs(targets) do
    local old, fam = item.tool, item.fam
    local setting = settingFor(fam)
    if not setting then
        skipped = skipped + 1
    else
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
            skipped = skipped + 1
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
            -- keep the instance name unless it carries the old family prefix, which would mislead the bridge
            local keep = oldName:gsub("^Kalyx_", "Velaria_"):gsub("^Skylight_FactWindow", "Velaria_CanvasControl")
            new:SetAttrs({ TOOLS_Name = keep })
            if px and py then flow:SetPos(new, px, py) end
            upgraded = upgraded + 1
        end
    end
end
comp:EndUndo(true)
comp:Unlock()
print(string.format("[Skylight] upgraded %d macro instance(s), skipped %d", upgraded, skipped))
