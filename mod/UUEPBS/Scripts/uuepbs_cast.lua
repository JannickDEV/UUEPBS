-- UUEPBS - which actors are characters (the window's "Choose character" list)
--
-- The list is built from FindAllOf("Character") (config.lua CandidateClasses), and games derive all sorts
-- of things from Character: boats, cameras, sequence helpers (Carnal Instinct: BP_Boat_B_C,
-- BP_SequenceHelper_C). Instead of a list of names per game, an actor is listed when it looks like a body:
--
--   1. It is placed in a level. Every live actor's full name runs through a level ("...:PersistentLevel.X");
--      a template a level sequence spawns from lives inside the sequence instead and is never seen in game.
--   2. One of its skeletal meshes has a mesh assigned whose bones name at least two body regions out of
--      head (head, neck, skull, jaw), torso (spine, pelvis, hips, chest...), arms (clavicle, upperarm,
--      hand, finger...) and legs (thigh, calf, knee, foot...). Names are split into words, so "lShldrBend",
--      "upperarm_l", "Bip001 L Hand", "mixamorig:LeftArm" and "ship_hull" read as people would read them
--      (the last is not a hip). Japanese / Chinese bone names count too (頭, 首, 腕, 手, 足, 脚, 腰, 胸...).
--      People, animals and monsters all pass; a boat's hull, a camera rig or an empty helper does not.
--
-- What cannot be read (no bone functions, a UE4SS build that cannot list components) counts as a
-- character: this only ever hides what it is sure about. The player, the game profile's Target and every
-- character with sliders are always listed (main.lua does not ask for those).
--
-- config.lua: CharacterListFilter = false lists everything again; CharacterListShow / CharacterListHide
-- (name patterns, * as wildcard, matched against the actor's object and class names) override the rule.
-- A verdict is kept per actor; a "no" is looked at again after RECHECK_MS (meshes are often assigned a
-- moment after the actor appears), a "yes" stands while the actor lives.

local C = {}

local ctx = nil
local byActor = {} -- actor address -> { full, ok, why, at }
local byMesh = {} -- mesh asset key -> { ok, why } (or false while unknown)
local said = {} -- actor full name -> true (the "hidden" line is logged once per actor)
local RECHECK_MS = 5000
local MAX_BONES = 512

-- Body regions and the words that name them. Words of 5+ letters also match as the start of a word
-- ("collarbone", "spineupper"); shorter ones must be the whole word ("hand" is not "handle", "hip" is
-- not "ship").
local REGIONS = {
    head = { "head", "neck", "skull", "jaw", "cranium" },
    torso = { "spine", "pelvis", "hip", "hips", "chest", "abdomen", "torso", "waist", "belly", "rib", "ribs", "stomach", "sternum" },
    arm = { "arm", "arms", "upperarm", "lowerarm", "forearm", "hand", "hands", "clavicle", "shoulder", "shldr", "elbow", "wrist",
        "finger", "fingers", "thumb", "collar", "palm" },
    leg = { "leg", "legs", "thigh", "calf", "knee", "foot", "feet", "toe", "toes", "shin", "ankle", "heel" },
}
-- CJK bone names (MMD / Chinese rigs): found anywhere in the name.
local CJK = {
    head = { "頭", "首", "头", "颈", "頸" },
    torso = { "腰", "胸", "上半身", "下半身", "脊", "骨盤", "骨盆" },
    arm = { "腕", "手", "肩", "指", "臂" },
    leg = { "足", "脚", "膝", "腿", "ひざ" },
}
local WORD = {} -- word -> region
local PREFIX = {} -- { word, region } for words of 5+ letters
for region, words in pairs(REGIONS) do
    for _, w in ipairs(words) do
        WORD[w] = region
        if #w >= 5 then
            PREFIX[#PREFIX + 1] = { w, region }
        end
    end
end

-- "lShldrBend" -> { "l", "shldr", "bend" }; "Bip001 L UpperArm" -> { "bip", "l", "upper", "arm" }.
function C.words(name)
    local s = tostring(name or "")
    s = s:gsub("(%l)(%u)", "%1 %2"):gsub("(%u)(%u%l)", "%1 %2")
    local out = {}
    for w in s:gmatch("%a+") do
        out[#out + 1] = string.lower(w)
    end
    return out
end

-- The body region a bone name names, or nil.
function C.region(name)
    for _, w in ipairs(C.words(name)) do
        local r = WORD[w]
        if r then
            return r
        end
        for _, p in ipairs(PREFIX) do
            if w:sub(1, #p[1]) == p[1] then
                return p[2]
            end
        end
    end
    local raw = tostring(name or "")
    if raw:find("[\128-\255]") then
        for region, list in pairs(CJK) do
            for _, w in ipairs(list) do
                if raw:find(w, 1, true) then
                    return region
                end
            end
        end
    end
    return nil
end

-- Do these bone names make a body? Returns ok, and what was found.
function C.body_of(names)
    local found, count = {}, 0
    for _, n in ipairs(names) do
        local r = C.region(n)
        if r and not found[r] then
            found[r] = true
            count = count + 1
            if count >= 2 then
                return true, count
            end
        end
    end
    return false, count
end

local function wildcard(pattern)
    local p = string.lower(tostring(pattern)):gsub("[%^%$%(%)%%%.%[%]%+%-%?]", "%%%0"):gsub("%*", ".*")
    return "^" .. p .. "$"
end

local function compile(list)
    local out = {}
    if type(list) == "table" then
        for _, v in ipairs(list) do
            if type(v) == "string" and v ~= "" then
                out[#out + 1] = wildcard(v)
            end
        end
    end
    return out
end

local showList, hideList = {}, {}

local function matches(list, names)
    for _, pat in ipairs(list) do
        for _, n in ipairs(names) do
            if n ~= "" and string.lower(n):find(pat) then
                return true
            end
        end
    end
    return false
end

-- ctx: { enabled = function() (default on), show, hide (pattern lists), now = function() ms, chatter,
--        full_name(actor), name_of(obj), class_name(actor), address(actor),
--        skeletal_meshes(actor) -> list (nil when they cannot be listed),
--        mesh_key(comp) -> key of its mesh asset (nil: none assigned),
--        bone_count(comp) -> number (nil when it cannot be read), bone_name(comp, index) -> string or nil }
function C.setup(c)
    ctx = c
    byActor, byMesh, said = {}, {}, {}
    showList, hideList = compile(c.show), compile(c.hide)
end

-- Bones of one mesh: true / false (and why), or nil when they cannot be read.
local function mesh_verdict(comp)
    local key = ctx.mesh_key(comp)
    if not key then
        return false, "no mesh assigned"
    end
    local v = byMesh[key]
    if v then
        return v.ok, v.why
    end
    local count = ctx.bone_count(comp)
    if count == nil then
        return nil
    end
    -- read until two regions are found (a body usually names its pelvis, spine and neck first)
    local found, regions = {}, 0
    for i = 0, math.min(count, MAX_BONES) - 1 do
        local n = ctx.bone_name(comp, i)
        if n == nil then
            return nil
        end
        local r = C.region(n)
        if r and not found[r] then
            found[r] = true
            regions = regions + 1
            if regions >= 2 then
                break
            end
        end
    end
    local ok = regions >= 2
    v = { ok = ok, why = ok and "" or string.format("%d bone(s), %s", count,
        regions == 0 and "none named like a head, spine, arm or leg" or "only one body region") }
    byMesh[key] = v
    return v.ok, v.why
end

local function judge(actor)
    local engine = ctx.name_of(actor) or ""
    local cls = ctx.class_name and ctx.class_name(actor) or ""
    if matches(showList, { engine, cls }) then
        return true
    end
    if matches(hideList, { engine, cls }) then
        return false, "CharacterListHide in config.lua"
    end
    local full = ctx.full_name(actor)
    if full and not full:find("PersistentLevel.", 1, true) then
        return false, "not placed in a level (a template)"
    end
    local comps = ctx.skeletal_meshes(actor)
    if comps == nil then
        return true -- cannot tell
    end
    if #comps == 0 then
        return false, "no skeletal mesh"
    end
    local why = nil
    for _, comp in ipairs(comps) do
        local ok, w = mesh_verdict(comp)
        if ok == nil or ok then
            return true -- a body, or bones that cannot be read
        end
        why = why or w
    end
    return false, "no body skeleton (" .. tostring(why) .. ")"
end

-- Is `actor` a character for the list? Returns ok, and why not.
function C.is_character(actor)
    if not ctx or (ctx.enabled and not ctx.enabled()) then
        return true
    end
    local addr = ctx.address(actor)
    if not addr then
        return true
    end
    local full = ctx.full_name(actor) or ""
    local now = ctx.now()
    local v = byActor[addr]
    if v and v.full == full and (v.ok or now - v.at < RECHECK_MS) then
        return v.ok, v.why
    end
    local okJ, ok, why = pcall(judge, actor)
    if not okJ then
        ok, why = true, nil -- something unexpected: list it
    end
    byActor[addr] = { full = full, ok = ok, why = why, at = now }
    if not ok and not said[full] then
        said[full] = true
        if ctx.chatter then
            ctx.chatter("Choose character: %s left out, %s", tostring(ctx.name_of(actor)), tostring(why))
        end
    end
    return ok, why
end

-- Forget actors that are gone (called with the set of addresses still around).
function C.prune(alive)
    for addr in pairs(byActor) do
        if not alive[addr] then
            byActor[addr] = nil
        end
    end
end

return C
