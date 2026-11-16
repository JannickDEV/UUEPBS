-- UUEPBS - mesh deformers that cannot show scaled bones
--
-- Some games draw a character through a Mesh Deformer (a Deformer Graph, UE 5.2+) instead of the
-- engine's own skinning. Afterparty's Daz characters use one for dual quaternion skinning
-- ("DG_DQS_JiggleColors"). Dual quaternion skinning only knows rotation and translation: a bone the
-- sliders scale (Head size, Hand size, Height...) tears the mesh apart (the face ends up between the
-- feet), while meshes without the deformer (eyes, hair, clothes) scale correctly.
--
-- So while a character's sliders scale bones, its meshes' deformers are switched off
-- (USkinnedMeshComponent::SetMeshDeformer(nil): the engine's normal skinning, which handles scale).
-- When the scaling goes (sliders back to 1, Before, sliders off, the character released) the
-- game's own deformer is put back. Whatever else the deformer did (jiggle, wrinkle colours) is
-- missing meanwhile; config.lua "MeshDeformers" = "keep" leaves deformers alone.
--
-- The DLL says which characters scale bones ("scaled <actor key>" lines in bridge_out.txt).

local D = {}

local ctx = nil
local saved = {} -- component address -> { comp, deformer, label, name }

local function alive(o)
    local ok, v = pcall(function()
        return o ~= nil and o:IsValid()
    end)
    return ok and v == true
end

local function address(o)
    local ok, a = pcall(function()
        return o:GetAddress()
    end)
    return ok and a or nil
end

local function name_of(o)
    local ok, n = pcall(function()
        return o:GetFName():ToString()
    end)
    return ok and n or "?"
end

local function deformer_of(comp)
    local ok, d = pcall(function()
        return comp.MeshDeformer
    end)
    if ok and alive(d) then
        return d
    end
    return nil
end

local function set_deformer(comp, deformer)
    local ok, err = pcall(function()
        comp:SetMeshDeformer(deformer)
    end)
    if not ok and deformer == nil and CreateInvalidObject then
        -- some UE4SS builds want an invalid object rather than nil for a null object parameter
        ok, err = pcall(function()
            comp:SetMeshDeformer(CreateInvalidObject())
        end)
    end
    return ok, err
end

local failed = {} -- component address -> true: switching off did not work, said so once

function D.setup(c)
    ctx = c
end

local function say(fmt, ...)
    if ctx and ctx.say then
        ctx.say(fmt, ...)
    end
end

-- rigs: the tracked meshes ({ comp, label, actorKey, ... }). scaled: set of actor keys whose
-- sliders scale bones. mode: "auto" (default) or "keep". Returns how many deformers changed.
function D.apply(rigs, scaled, mode)
    local changed = 0
    local want = {} -- component address -> rig, for meshes whose deformer must be off
    if mode ~= "keep" then
        for _, r in ipairs(rigs or {}) do
            if r.actorKey and scaled[r.actorKey] and alive(r.comp) then
                local a = address(r.comp)
                if a then
                    want[a] = r
                end
            end
        end
    end
    for a in pairs(failed) do
        if not want[a] then
            failed[a] = nil -- try again the next time it is scaled
        end
    end
    -- put back what is no longer scaled (or no longer tracked)
    for a, s in pairs(saved) do
        if not want[a] then
            if alive(s.comp) and deformer_of(s.comp) == nil then
                local ok, err = set_deformer(s.comp, s.deformer)
                if ok then
                    say("mesh deformer %s back on %s", s.name, s.label)
                    changed = changed + 1
                else
                    say("could not put mesh deformer %s back on %s: %s", s.name, s.label, tostring(err))
                end
            end
            saved[a] = nil
        end
    end
    -- switch off the ones that are scaled now
    for a, r in pairs(want) do
        if not saved[a] then
            local d = deformer_of(r.comp)
            if d then
                local ok, err = set_deformer(r.comp, nil)
                if ok and deformer_of(r.comp) == nil then
                    saved[a] = { comp = r.comp, deformer = d, label = r.label or name_of(r.comp), name = name_of(d) }
                    failed[a] = nil
                    say("mesh deformer %s off on %s while the sliders scale bones (it cannot show scaled bones; MeshDeformers in config.lua)",
                        name_of(d), r.label or name_of(r.comp))
                    changed = changed + 1
                elseif not failed[a] then
                    failed[a] = true
                    say("could not switch mesh deformer %s off on %s: %s", name_of(d), r.label or name_of(r.comp),
                        ok and "it is still set" or tostring(err))
                end
            end
        elseif saved[a].comp ~= r.comp then
            saved[a].comp = r.comp
        end
    end
    return changed
end

-- Puts every deformer back (script shutdown, sliders reset).
function D.restore_all()
    return D.apply({}, {}, "keep")
end

-- For "ubs status".
function D.status()
    local n, names = 0, {}
    for _, s in pairs(saved) do
        n = n + 1
        names[#names + 1] = s.name .. " on " .. s.label
    end
    if n == 0 then
        return "mesh deformers: none switched off"
    end
    table.sort(names)
    return "mesh deformers switched off while bones are scaled: " .. table.concat(names, ", ")
end

return D
