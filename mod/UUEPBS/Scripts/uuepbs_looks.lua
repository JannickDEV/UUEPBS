-- UUEPBS - Looks: skin and hair recoloured through the outfit's own material parameters
--
-- The DLL sends looks along with the morph weights, as "look:<id>" names (core/looks.hpp):
--   look:skin.tone        -2..2   below 0 tans, above 0 lightens (scales the skin's tint colour)
--   look:skin.sweat        0..2   wet sheen: roughness down, wetness / specular up
--   look:hair.hue         -1..1   turns the hair colour around the colour wheel (1 = 180 degrees)
--   look:hair.saturation  -2..2   greyer / stronger hair colour
--   look:hair.lightness   -2..2   darker / lighter hair (MetaHuman hair: more / less melanin)
-- A look that is absent leaves the material alone.
--
-- How: for every material slot of a character's tracked meshes, the slot is classed as skin, hair
-- or neither from its slot name and material name. The parameters its material instance chain
-- overrides (ScalarParameterValues / VectorParameterValues) are matched against name patterns,
-- and the slot gets a dynamic material instance (UPrimitiveComponent::CreateDynamicMaterialInstance,
-- which reuses the game's own one if the slot already has it) with the shifted values. The values
-- found first are remembered; resetting a look writes them back and gives the slot its original
-- material again. Textures are never replaced.
--
-- The game profile can name other slots and parameters (Scripts\GameProfiles\<game>.json):
--   "Looks": {
--     "Skin": { "Slots": ["*body*", "*head*"], "ExcludeSlots": ["*eye*"], "Tint": ["SkinColor"],
--               "Wet": ["Wetness"], "Roughness": ["Roughness*"], "Specular": ["Specular"] },
--     "Hair": { "Slots": ["*hair*"], "Tint": ["RootColor", "TipColor"], "Melanin": ["Melanin"], "Redness": ["Redness"] }
--   }
-- Patterns are case-insensitive, * = anything. A name without * is also set when the material
-- does not list it (parameters only the parent UMaterial has cannot be listed at run time).

local L = {}

local ctx = nil
local PREFIX = "look:"

local DEFAULTS = {
    Skin = {
        Slots = { "*skin*", "*body*", "*head*", "*face*", "*torso*", "*arm*", "*leg*", "*hand*", "*foot*", "*feet*", "*neck*", "*chest*",
            "*nude*", "*naked*" },
        ExcludeSlots = { "*mouth*", "*teeth*", "*tooth*", "*tongue*", "*eye*", "*lash*", "*brow*", "*hair*", "*nail*", "*tear*", "*gum*",
            "*cloth*", "*armor*", "*armour*", "*glove*", "*boot*", "*shoe*", "*sock*", "*cornea*", "*iris*", "*occlusion*", "*shadow*" },
        Tint = { "*skin*tint*", "*skin*col*", "*skin*tone*", "*base*col*tint*", "*albedo*tint*", "*diffuse*tint*", "*tint*",
            "*basecol*", "*albedo*col*", "*diffuse*col*" },
        Brightness = { "*skin*bright*", "*albedo*bright*", "*brightness*" },
        Wet = { "*wet*", "*sweat*", "*moist*" },
        Roughness = { "*rough*" },
        Specular = { "*specular*", "*spec*int*" },
    },
    Hair = {
        Slots = { "*hair*", "*brow*", "*lash*", "*beard*", "*mustache*", "*moustache*", "*ponytail*", "*bang*", "*fringe*" },
        ExcludeSlots = {},
        Tint = { "*hair*col*", "*hair*tint*", "*root*col*", "*tip*col*", "*tint*", "*basecol*", "*albedo*col*", "*diffuse*col*" },
        Brightness = { "*hair*bright*", "*brightness*" },
        Melanin = { "*melanin*" },
        Redness = { "*redness*" },
    },
}
-- Never touched, whatever matched: glows, outlines and masks are not colours of the skin or hair.
local NEVER = { "*emiss*", "*glow*", "*outline*", "*rim*", "*fresnel*", "*mask*", "*uv*", "*tiling*", "*scale*", "*index*", "*switch*",
    "*use*", "*enable*", "*detail*" }

local records = {} -- "<comp address>|<slot>" -> { comp, full, index, original, mid, created, kind, params = { name -> { vector, orig } } }
local paramCache = {} -- material address -> { scalars = { lower -> name }, vectors = { lower -> name }, said }
local patternCache = {}
local lastWanted = {} -- actor key -> { id -> value } (the looks only)
local reportLines = {} -- actor key -> text
local reportText = ""

-- ------------------------------------------------------------- helpers

local function chatter(fmt, ...)
    if ctx and ctx.chatter then
        ctx.chatter("looks: " .. fmt, ...)
    end
end

local function say(fmt, ...)
    if ctx and ctx.say then
        ctx.say("looks: " .. fmt, ...)
    end
end

local function valid(obj)
    local ok, v = pcall(function()
        return obj ~= nil and obj:IsValid()
    end)
    return ok and v == true
end

local function addr(obj)
    local ok, a = pcall(function()
        return obj:GetAddress()
    end)
    return ok and a or nil
end

local function fname_text(obj)
    local ok, s = pcall(function()
        return obj:GetFName():ToString()
    end)
    return ok and type(s) == "string" and s or ""
end

local function name_text(n)
    if type(n) == "string" then
        return n
    end
    local ok, s = pcall(function()
        return n:ToString()
    end)
    return ok and type(s) == "string" and s or ""
end

-- "*skin*col*" -> Lua pattern, anchored, case-insensitive (callers lower the text).
local function compile(pattern)
    local c = patternCache[pattern]
    if c == nil then
        local p = string.lower(pattern):gsub("[%^%$%(%)%%%.%[%]%+%-%?]", "%%%0"):gsub("%*", ".*")
        c = "^" .. p .. "$"
        patternCache[pattern] = c
    end
    return c
end

local function matches(text, patterns)
    if type(patterns) ~= "table" then
        return false
    end
    local lower = string.lower(text)
    for _, p in ipairs(patterns) do
        if type(p) == "string" and lower:find(compile(p)) then
            return true
        end
    end
    return false
end

local function clamp(v, lo, hi)
    if v < lo then
        return lo
    end
    if v > hi then
        return hi
    end
    return v
end

-- The settings for one category: the game profile's "Looks" entry over the defaults.
local function settings(kind)
    local s = {}
    for k, v in pairs(DEFAULTS[kind]) do
        s[k] = v
    end
    local profile = ctx and ctx.profile and ctx.profile() or nil
    local looks = profile and profile.Looks
    local own = type(looks) == "table" and looks[kind] or nil
    if type(own) == "table" then
        for k, v in pairs(own) do
            if type(v) == "table" then
                s[k] = v
            end
        end
    end
    return s
end

-- ------------------------------------------------------------- colour maths (exposed for tests)

function L.rgb_to_hsv(r, g, b)
    local mx, mn = math.max(r, g, b), math.min(r, g, b)
    local d = mx - mn
    local h = 0
    if d > 1e-6 then
        if mx == r then
            h = ((g - b) / d) % 6
        elseif mx == g then
            h = (b - r) / d + 2
        else
            h = (r - g) / d + 4
        end
        h = h / 6
    end
    local s = mx > 1e-6 and d / mx or 0
    return h, s, mx
end

function L.hsv_to_rgb(h, s, v)
    h = (h % 1) * 6
    local i = math.floor(h)
    local f = h - i
    local p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    if i == 0 then
        return v, t, p
    elseif i == 1 then
        return q, v, p
    elseif i == 2 then
        return p, v, t
    elseif i == 3 then
        return p, q, v
    elseif i == 4 then
        return t, p, v
    end
    return v, p, q
end

-- Skin tone: < 0 tans (red kept most, blue least: warmer), > 0 lightens evenly.
function L.skin_tint(c, tone)
    local f = 2 ^ (tone * 0.35)
    if tone < 0 then
        return { R = c.R * f ^ 0.8, G = c.G * f, B = c.B * f ^ 1.25, A = c.A }
    end
    return { R = c.R * f, G = c.G * f, B = c.B * f, A = c.A }
end

-- Hair: hue turn (1 = 180 degrees), saturation and lightness steps (each 1 = x2 / x0.7).
function L.hair_tint(c, hue, sat, light)
    local h, s, v = L.rgb_to_hsv(c.R, c.G, c.B)
    if hue ~= 0 and s < 0.05 then
        -- a grey tint has no hue to turn: start from a warm brown so the slider shows something
        h, s = 0.08, math.min(1, 0.6 * math.abs(hue))
    end
    h = h + hue * 0.5
    s = clamp(s * 2 ^ sat, 0, 1)
    v = v * 2 ^ (light * 0.5)
    local r, g, b = L.hsv_to_rgb(h, s, v)
    return { R = r, G = g, B = b, A = c.A }
end

-- One scalar parameter's new value. role: Brightness / Wet / Roughness / Specular / Melanin / Redness.
function L.scalar_value(role, orig, look)
    if role == "Brightness" then
        if look.kind == "skin" then
            return orig * 2 ^ (look.tone * 0.35)
        end
        return orig * 2 ^ (look.light * 0.5)
    elseif role == "Wet" then
        return orig + (math.max(1, orig) - orig) * (look.sweat / 2)
    elseif role == "Roughness" then
        return math.max(0.04, orig * (1 - 0.6 * look.sweat / 2))
    elseif role == "Specular" then
        return orig * (1 + 0.75 * look.sweat / 2)
    elseif role == "Melanin" then
        return clamp(orig - 0.25 * look.light, 0, 1)
    elseif role == "Redness" then
        return clamp(orig * 2 ^ look.sat + 0.5 * look.hue, 0, 1)
    end
    return orig
end

-- ------------------------------------------------------------- materials

local function array_each(arr, fn)
    if arr == nil then
        return
    end
    pcall(function()
        if arr.ForEach then
            arr:ForEach(function(_, elem)
                local e = elem
                pcall(function()
                    if elem.get then
                        e = elem:get()
                    end
                end)
                fn(e)
            end)
        else
            for i = 1, #arr do
                fn(arr[i])
            end
        end
    end)
end

-- Parameter names a material instance chain overrides (scalars and vectors), cached per material.
local function params_of(material)
    local key = addr(material)
    if key and paramCache[key] then
        return paramCache[key]
    end
    local out = { scalars = {}, vectors = {} }
    local m = material
    for _ = 1, 8 do
        if not valid(m) then
            break
        end
        local scalars, vectors
        pcall(function()
            scalars = m.ScalarParameterValues
        end)
        pcall(function()
            vectors = m.VectorParameterValues
        end)
        array_each(scalars, function(p)
            local ok, n = pcall(function()
                return name_text(p.ParameterInfo.Name)
            end)
            if ok and n ~= "" and n ~= "None" then
                out.scalars[string.lower(n)] = n
            end
        end)
        array_each(vectors, function(p)
            local ok, n = pcall(function()
                return name_text(p.ParameterInfo.Name)
            end)
            if ok and n ~= "" and n ~= "None" then
                out.vectors[string.lower(n)] = n
            end
        end)
        local parent
        pcall(function()
            parent = m.Parent
        end)
        if not valid(parent) or addr(parent) == addr(m) then
            break
        end
        m = parent
    end
    if key then
        paramCache[key] = out
    end
    return out
end

-- Names of `pool` (lower -> name) matched by the first pattern that matches any; plus explicit
-- (wildcard-free) names even when not listed. Names matching NEVER are left out.
function L.pick(pool, patterns)
    local found = {}
    if type(patterns) ~= "table" then
        return found
    end
    for _, p in ipairs(patterns) do
        if type(p) == "string" then
            local c = compile(p)
            for lower, name in pairs(pool) do
                if lower:find(c) and not matches(lower, NEVER) then
                    found[#found + 1] = name
                end
            end
            if #found == 0 and not p:find("*", 1, true) then
                found[1] = p
            end
            if #found > 0 then
                table.sort(found)
                return found
            end
        end
    end
    return found
end

local function slot_names(comp)
    local names = {}
    pcall(function()
        local list = comp:GetMaterialSlotNames()
        local i = 0
        array_each(list, function(n)
            names[i] = name_text(n)
            i = i + 1
        end)
    end)
    return names
end

-- "skin", "hair" or nil, from "<slot name>|<material name>".
function L.classify(text, skin, hair)
    if matches(text, hair.Slots) and not matches(text, hair.ExcludeSlots) then
        return "hair"
    end
    if matches(text, skin.Slots) and not matches(text, skin.ExcludeSlots) then
        return "skin"
    end
    return nil
end

local function get_scalar(mid, name)
    local ok, v = pcall(function()
        return mid:K2_GetScalarParameterValue(FName(name))
    end)
    return ok and type(v) == "number" and v or nil
end

local function get_vector(mid, name)
    local ok, c = pcall(function()
        local v = mid:K2_GetVectorParameterValue(FName(name))
        return { R = v.R, G = v.G, B = v.B, A = v.A }
    end)
    if ok and type(c) == "table" and type(c.R) == "number" then
        return c
    end
    return nil
end

local function set_scalar(mid, name, v)
    return pcall(function()
        mid:SetScalarParameterValue(FName(name), v)
    end)
end

local function set_vector(mid, name, c)
    return pcall(function()
        mid:SetVectorParameterValue(FName(name), { R = c.R, G = c.G, B = c.B, A = c.A or 1 })
    end)
end

local function current_material(comp, index)
    local ok, m = pcall(function()
        return comp:GetMaterial(index)
    end)
    return ok and valid(m) and m or nil
end

local function make_mid(comp, index, material)
    local ok, mid = pcall(function()
        return comp:CreateDynamicMaterialInstance(index, material, FName("None"))
    end)
    if not (ok and valid(mid)) then
        ok, mid = pcall(function()
            return comp:CreateDynamicMaterialInstance(index, material)
        end)
    end
    return ok and valid(mid) and mid or nil
end

local function restore(rec)
    if not (valid(rec.comp) and valid(rec.mid)) then
        return
    end
    for name, p in pairs(rec.params) do
        if p.vector then
            set_vector(rec.mid, name, p.orig)
        else
            set_scalar(rec.mid, name, p.orig)
        end
    end
    if rec.created and valid(rec.original) then
        local cur = current_material(rec.comp, rec.index)
        if cur and addr(cur) == addr(rec.mid) then
            pcall(function()
                rec.comp:SetMaterial(rec.index, rec.original)
            end)
        end
    end
end

-- The look values of one character, or nil when every one is neutral.
local function look_of(wanted)
    local v = { tone = 0, sweat = 0, hue = 0, sat = 0, light = 0 }
    local any = false
    if type(wanted) == "table" then
        local map = { ["skin.tone"] = "tone", ["skin.sweat"] = "sweat", ["hair.hue"] = "hue", ["hair.saturation"] = "sat", ["hair.lightness"] = "light" }
        for key, want in pairs(wanted) do
            if key:sub(1, #PREFIX) == PREFIX then
                local field = map[key:sub(#PREFIX + 1)]
                local w = tonumber(want.weight)
                if field and w and w == w and math.abs(w) > 1e-4 then
                    v[field] = w
                    any = true
                end
            end
        end
    end
    return any and v or nil
end
L.look_of = look_of

-- What one slot should get: { name -> { vector = bool, role } }, empty when nothing matches.
local function plan_slot(kind, look, pool, s)
    local plan = {}
    local function add(role, vector, names)
        for _, n in ipairs(names) do
            plan[n] = plan[n] or { vector = vector, role = role }
        end
    end
    if kind == "skin" then
        if look.tone ~= 0 then
            local tints = L.pick(pool.vectors, s.Tint)
            add("Tint", true, tints)
            if #tints == 0 then
                add("Brightness", false, L.pick(pool.scalars, s.Brightness))
            end
        end
        if look.sweat ~= 0 then
            add("Wet", false, L.pick(pool.scalars, s.Wet))
            add("Roughness", false, L.pick(pool.scalars, s.Roughness))
            add("Specular", false, L.pick(pool.scalars, s.Specular))
        end
    else
        if look.hue ~= 0 or look.sat ~= 0 or look.light ~= 0 then
            local tints = L.pick(pool.vectors, s.Tint)
            add("Tint", true, tints)
            add("Melanin", false, L.pick(pool.scalars, s.Melanin))
            add("Redness", false, L.pick(pool.scalars, s.Redness))
            if #tints == 0 and next(plan) == nil then
                add("Brightness", false, L.pick(pool.scalars, s.Brightness))
            end
        end
    end
    return plan
end
L.plan_slot = plan_slot

-- ------------------------------------------------------------- applying

-- rigs: main.lua's tracked meshes ({ comp, full, address, actorKey, label }).
-- wantedByActor: actor key -> folded name -> { name, weight } (the morph book; looks are picked out).
function L.apply(rigs, wantedByActor)
    local skin, hair = settings("Skin"), settings("Hair")
    local seen = {}
    local report = {}
    lastWanted = wantedByActor or {}
    for _, r in ipairs(rigs or {}) do
        local look = look_of(lastWanted[r.actorKey])
        local comp = r.comp
        if look and valid(comp) then
            local rep = report[r.actorKey] or { meshes = 0, skin = 0, hair = 0, roles = {}, skinSlots = 0, hairSlots = 0 }
            report[r.actorKey] = rep
            local touched = false
            local names = slot_names(comp)
            local count = 0
            pcall(function()
                count = comp:GetNumMaterials()
            end)
            for index = 0, math.min(count or 0, 64) - 1 do
                local key = tostring(r.address) .. "|" .. index
                local rec = records[key]
                local cur = current_material(comp, index)
                if rec and cur and addr(cur) ~= addr(rec.mid) then
                    -- the game put another material in the slot (re-dressed): start over from it
                    records[key] = nil
                    rec = nil
                end
                local base = rec and rec.original or cur
                if base then
                    local text = (names[index] or "") .. "|" .. fname_text(base)
                    local kind = L.classify(text, skin, hair)
                    if kind == "skin" then
                        rep.skinSlots = rep.skinSlots + 1
                    elseif kind == "hair" then
                        rep.hairSlots = rep.hairSlots + 1
                    end
                    local pool = kind and params_of(base) or nil
                    if pool and not pool.said then
                        pool.said = true
                        local list = {}
                        for _, n in pairs(pool.vectors) do
                            list[#list + 1] = n .. " (colour)"
                        end
                        for _, n in pairs(pool.scalars) do
                            list[#list + 1] = n
                        end
                        table.sort(list)
                        chatter("%s slot %s: %s", kind, text, #list > 0 and table.concat(list, ", ") or "no listed parameters")
                    end
                    local plan = kind and plan_slot(kind, look, pool, kind == "skin" and skin or hair) or {}
                    if next(plan) ~= nil then
                        if not rec then
                            local mid = make_mid(comp, index, cur)
                            if mid then
                                rec = { comp = comp, full = r.full, index = index, original = cur, mid = mid, created = addr(mid) ~= addr(cur), kind = kind,
                                    params = {} }
                                records[key] = rec
                            end
                        end
                        if rec then
                            seen[key] = true
                            local any = false
                            for name, p in pairs(plan) do
                                local entry = rec.params[name]
                                if not entry then
                                    local orig = p.vector and get_vector(rec.mid, name) or (not p.vector and get_scalar(rec.mid, name))
                                    if orig then
                                        entry = { vector = p.vector, orig = orig }
                                        rec.params[name] = entry
                                    end
                                end
                                if entry then
                                    local ok
                                    if p.vector then
                                        local c = kind == "skin" and L.skin_tint(entry.orig, look.tone) or L.hair_tint(entry.orig, look.hue, look.sat, look.light)
                                        ok = set_vector(rec.mid, name, c)
                                    else
                                        look.kind = kind
                                        ok = set_scalar(rec.mid, name, L.scalar_value(p.role, entry.orig, look))
                                    end
                                    if ok then
                                        any = true
                                        rep.roles[string.lower(p.role)] = true
                                    end
                                end
                            end
                            -- parameters of an earlier look that this one no longer moves go back
                            for name, entry in pairs(rec.params) do
                                if not plan[name] then
                                    if entry.vector then
                                        set_vector(rec.mid, name, entry.orig)
                                    else
                                        set_scalar(rec.mid, name, entry.orig)
                                    end
                                end
                            end
                            if any then
                                touched = true
                                rep[kind] = rep[kind] + 1
                            end
                        end
                    end
                end
            end
            if touched then
                rep.meshes = rep.meshes + 1
            end
        end
    end
    -- Slots no longer wanted (look reset, character released, mesh gone): put them back.
    for key, rec in pairs(records) do
        if not seen[key] then
            restore(rec)
            records[key] = nil
        end
    end
    -- One line per character for the window.
    local lines, keys = {}, {}
    for k in pairs(report) do
        keys[#keys + 1] = k
    end
    table.sort(keys)
    reportLines = {}
    for _, k in ipairs(keys) do
        local rep = report[k]
        local roles = {}
        for role in pairs(rep.roles) do
            roles[#roles + 1] = role
        end
        table.sort(roles)
        local parts = {}
        local slots = rep.skin + rep.hair
        if slots > 0 then
            parts[#parts + 1] = string.format("%d mesh%s: %d slot%s changed (%s)", rep.meshes, rep.meshes == 1 and "" or "es", slots, slots == 1 and "" or "s",
                table.concat(roles, ", "))
        else
            parts[#parts + 1] = "nothing could be changed yet"
        end
        if rep.skinSlots == 0 then
            parts[#parts + 1] = "no skin slots found on these meshes"
        elseif rep.skin == 0 then
            parts[#parts + 1] = string.format("%d skin slot%s without matching parameters", rep.skinSlots, rep.skinSlots == 1 and "" or "s")
        end
        if rep.hairSlots == 0 then
            parts[#parts + 1] = "no hair slots found on these meshes"
        elseif rep.hair == 0 then
            parts[#parts + 1] = string.format("%d hair slot%s without matching parameters", rep.hairSlots, rep.hairSlots == 1 and "" or "s")
        end
        local text = table.concat(parts, "; ")
        reportLines[k] = text
        lines[#lines + 1] = "looks\t" .. k .. "\t" .. text:gsub("[\t\r\n]", " ")
    end
    local newText = table.concat(lines, "\n")
    local changed = newText ~= reportText
    reportText = newText
    return changed
end

-- Cheap check between applies: did the game swap a recoloured slot's material (re-dressing)?
-- Returns true when the looks need applying again.
function L.watch()
    for _, rec in pairs(records) do
        if not valid(rec.comp) then
            return true
        end
        local cur = current_material(rec.comp, rec.index)
        if cur and addr(cur) ~= addr(rec.mid) then
            return true
        end
    end
    return false
end

-- Lines for bridge_in.txt ("looks <tab> key <tab> text").
function L.report()
    return reportText
end

function L.report_for(key)
    return reportLines[key]
end

function L.active()
    return next(records) ~= nil
end

function L.setup(c)
    ctx = c
    records, paramCache, reportLines, reportText = {}, {}, {}, ""
end

return L
