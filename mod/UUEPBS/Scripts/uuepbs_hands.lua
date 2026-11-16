-- UUEPBS - what each edited character is playing, for Hand Contact
--
-- Hand Contact (done by the DLL inside the pose) carries a hand that rests on the body in the game's
-- animation along with the edited body, and bends the arm to reach it. It works from the bones alone.
-- This module only tells the DLL which animation each tracked character plays right now (its active
-- montage, else the animation a single-node mesh plays), so that:
--   * config.lua HandContactAnims can limit Hand Contact to certain animations (by name), and
--   * "ubs hands" can show which animation is playing next to where the hands rest.
-- Sent as "play <actor key> <name>" lines in bridge_in.txt.

local H = {}

local playing = {} -- actor key -> animation name

local function alive(o)
    local ok, v = pcall(function()
        return o ~= nil and o:IsValid()
    end)
    return ok and v == true
end

local function name_of(o)
    local ok, n = pcall(function()
        return o:GetFName():ToString()
    end)
    return ok and n or nil
end

local function clean(s)
    return (tostring(s or ""):gsub("[\t\r\n]", " "))
end

-- The montage the mesh's anim instance plays, else the animation of a single-node mesh, else nil.
function H.playing_on(comp)
    if not alive(comp) then
        return nil
    end
    local okI, inst = pcall(function()
        return comp:GetAnimInstance()
    end)
    if not okI or not alive(inst) then
        okI, inst = pcall(function()
            return comp.AnimScriptInstance
        end)
    end
    if okI and alive(inst) then
        local okM, m = pcall(function()
            return inst:GetCurrentActiveMontage()
        end)
        if okM and alive(m) then
            return name_of(m)
        end
    end
    local okA, anim = pcall(function()
        return comp.AnimationData.AnimToPlay
    end)
    if okA and alive(anim) then
        local okMode, mode = pcall(function()
            return comp:GetAnimationMode()
        end)
        -- EAnimationMode::AnimationSingleNode = 1
        if okMode and tonumber(mode) == 1 then
            return name_of(anim)
        end
    end
    return nil
end

-- Reads what every character's main mesh plays. True when that changed (the bridge file must be rewritten).
function H.poll(rigs)
    local now = {}
    for _, r in ipairs(rigs or {}) do
        if r.primary and not r.copy and r.actorKey and now[r.actorKey] == nil then
            local name = H.playing_on(r.comp)
            if name and name ~= "" and name ~= "None" then
                now[r.actorKey] = clean(name)
            end
        end
    end
    local changed = false
    for k, v in pairs(now) do
        if playing[k] ~= v then
            changed = true
        end
    end
    for k in pairs(playing) do
        if now[k] == nil then
            changed = true
        end
    end
    playing = now
    return changed
end

-- "play" lines for bridge_in.txt (sorted, so an unchanged state gives the same text).
function H.report()
    local keys = {}
    for k in pairs(playing) do
        keys[#keys + 1] = k
    end
    table.sort(keys)
    local lines = {}
    for _, k in ipairs(keys) do
        lines[#lines + 1] = "play\t" .. clean(k) .. "\t" .. playing[k]
    end
    return table.concat(lines, "\n")
end

function H.status()
    local keys = {}
    for k in pairs(playing) do
        keys[#keys + 1] = k
    end
    table.sort(keys)
    if #keys == 0 then
        return "hand contact: no montage or single animation playing on the tracked characters"
    end
    local parts = {}
    for _, k in ipairs(keys) do
        parts[#parts + 1] = k .. " plays " .. playing[k]
    end
    return "hand contact: " .. table.concat(parts, ", ")
end

function H.reset()
    playing = {}
end

return H
