-- UUEPBS - scene copies: the sliders also reach the copies of a character that cutscenes,
-- inventory / equipment screens, photo modes and other menus show.
--
-- Many games don't show the character you walk around with there: a cutscene spawns its own actor,
-- a menu shows a preview actor (often in front of a scene capture), each with its own skeletal mesh
-- components that use the same mesh assets. Those components are not the character the mod edits,
-- so they used to show the original body until you were back in the open world.
--
-- A "copy" is a skeletal mesh component that
--   * uses a mesh asset the mod sculpts on an edited character (the "source"),
--   * is not on a character that is already sculpted (the plan), and
--   * sits on an actor that looks like a copy of that character, not like an NPC that merely shares
--     a body mesh: its meshes include the character's main (body) mesh asset and at least two of the
--     character's sculpted mesh assets (one, when the character only has one), and it is not controlled
--     by the player. An AI-controlled actor needs two shared assets even when the character has only one
--     mesh (NPCs often share a body mesh). Shared eyes, eyelashes or hair alone never make a copy: in
--     games like Afterparty every NPC wears the same ones as the player.
--   * really uses that asset: the match is checked once more with GetSkinnedAsset before it is taken.
--   * when the actor is a pawn (an NPC or companion of its own, not a plain cutscene / menu actor), its body
--     looks like the character's: the main mesh's morph targets that shape the body (not the ones the sliders
--     set, nor expressions, correctives or other animated ones) have the same weights. In character-creator
--     games NPCs are built from the player's base body and outfits and only differ in those weights
--     (Afterparty's NPCs, two Carnal Instinct NPCs with the same body and clothes).
-- Copies are sent to the DLL as extra (non-primary) meshes of that character, so they get its bone
-- edits, proportions, morphs and looks. They are dropped as soon as they go away or change mesh.
--
-- Copies are found without walking the object array all the time:
--   * every new skeletal mesh component is looked at a few times while the game sets it up
--     (NotifyOnNewObject; a few property reads each),
--   * blueprints that put a mesh on a component (SetSkeletalMesh / SetSkinnedMeshAsset) are hooked,
--   * the world's skeletal meshes are searched in a short burst when the camera moves to another actor
--     (cutscenes), when the edited character stops being rendered (menus, loading screens, cutscenes
--     that hide it), and when a scene capture appears (3D previews in menus). While the character stays
--     out of view the search is repeated every few seconds, a limited number of times.
-- Config.SceneCopies = false turns all of it off. Config.SceneCopyMatch = "mesh" accepts a single shared
-- mesh asset everywhere (for games whose copies only reuse the body).
--
-- Rules (when the guess is wrong): an actor can be named "never a copy" or "always a copy", optionally "a copy
-- of <character>". They win over everything above. A rule names actors by identity (what survives reloads,
-- e.g. "Nenu"), engine name, class name or shown name, case-insensitive, with * as a wildcard ("CI_NPC_*").
--   never  <who>        never a copy (an NPC built from the character's parts)
--   copy   <who>        a copy of whichever edited character it shares mesh assets with, no other checks
--   copy   <who> <of>   a copy of character <of> ("player", an identity, a name, a wildcard), even when it
--                       shares no mesh asset with it (a cutscene actor with a mesh of its own): all of its
--                       skeletal meshes get that character's sliders
-- They come from the window (Status > Scene copies, Choose character right-click), the console
-- ("ubs copies never|copy|forget"), <presets>\_scene_copies.txt, config.lua and the game profile.

local C = {}

local ctx = nil
local CHECK_MS = 1000 -- how often the camera / rendering is looked at
local BURST = { 300, 1500, 4000 } -- searches after something changed (ms)
local AWAY_EVERY_MS = 4000 -- while the character stays out of view
local AWAY_MAX = 10 -- searches per time out of view
local MIN_GAP_MS = 900 -- between two world searches
local NEW_DELAYS = { 250, 1500, 5000 } -- looks at a new component (the game assigns its mesh later)
local MAX_PENDING = 512

local sources = {} -- mesh asset address -> { key, label, ownerLabel, ownerEngine }
local keyAssets = {} -- actor key -> number of distinct sculpted mesh assets
local mainAsset = {} -- actor key -> mesh asset address of the character's main (primary) mesh
local mainComp = {} -- actor key -> { comp, full, names } the character's main mesh and its morph target names
local tracked = {} -- component address -> true (meshes sculpted as part of a character)
local planned = {} -- actor address -> true (characters being sculpted)
local primary = nil -- { comp, full } the edited character's main mesh (for the rendering check)
local sourceSig = ""

local copies = {} -- component address -> { comp, full, asset, key, rig, owner }
local copyList = {} -- copies in a stable order (rigs())
local pending = {} -- { comp, due = { times }, step }
local searches = {} -- due times of world searches
local nextCheck = 0
local lastSearch = -1e9
local lastView = nil -- address of the camera's view target at the last check
local wasRendered = nil
local away = { on = false, count = 0, due = nil }
local unsupported = {} -- engine calls that failed ("view", "rendered") -> when to try again (ms)
local RETRY_MS = 30000
local stats = { searches = 0, found = 0, ms = 0, lastReason = "" }
local changed = false
local rejected = {} -- actor address -> { who, label, of, why, order }: turned down (shown in the window)
local rejectedOrder = 0
local MAX_REJECTED = 16
local rules = {} -- { mode = "never"|"copy", who, of, source, pattern (lowercased glob or nil), exact }
local hasTargeted = false -- a "copy <who> <of>" rule exists: actors are looked at even without shared assets
local keyInfo = {} -- actor key -> { names = { lowercased }, label, engine } of the characters being sculpted
local reportDirty = false

-- ------------------------------------------------------------- helpers

local function alive(obj)
    return ctx.alive(obj)
end

local function addr(obj)
    return obj ~= nil and ctx.address_num(obj) or nil
end

local function owner_of(comp)
    local owner
    pcall(function()
        owner = comp:GetOwner()
    end)
    if not alive(owner) then
        pcall(function()
            owner = comp:GetOuter()
        end)
    end
    return alive(owner) and owner or nil
end

-- 2 = controlled by the player, 1 = by an AI (or anything else), 0 = nobody.
local function controlled(actor)
    local c
    pcall(function()
        c = actor.Controller
    end)
    if not alive(c) then
        return 0
    end
    local pc = ctx.controller and ctx.controller()
    if pc and addr(pc) == addr(c) then
        return 2
    end
    local isPlayer = false
    pcall(function()
        isPlayer = c:IsPlayerController() == true
    end)
    return isPlayer and 2 or 1
end

local function skeletal_comps(actor)
    local out = {}
    local cls = ctx.skel_class and ctx.skel_class()
    if not cls then
        return out
    end
    pcall(function()
        ctx.each_of(actor:K2_GetComponentsByClass(cls), function(c)
            if alive(c) then
                out[#out + 1] = c
            end
        end)
    end)
    return out
end

local function sort_copies()
    copyList = {}
    for _, c in pairs(copies) do
        copyList[#copyList + 1] = c
    end
    table.sort(copyList, function(a, b)
        if a.key ~= b.key then
            return a.key < b.key
        end
        return a.order < b.order
    end)
end

local order = 0
local function drop(a, why)
    local c = copies[a]
    if c then
        copies[a] = nil
        changed = true
        reportDirty = true
        ctx.chatter("scene copy %s let go (%s)", c.rig and c.rig.engine or "?", why)
    end
end

-- ------------------------------------------------------------- rules

local function glob(pattern)
    return "^" .. pattern:gsub("[%^%$%(%)%%%.%[%]%+%-%?]", "%%%0"):gsub("%*", ".*") .. "$"
end

local function trim(text)
    return (tostring(text or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

-- One rule, ready for matching (nil when unusable).
local function compile_rule(r)
    local mode = string.lower(trim(r.mode))
    if mode == "always" or mode == "yes" then
        mode = "copy"
    elseif mode == "no" or mode == "not" then
        mode = "never"
    end
    local who = trim(r.who)
    if (mode ~= "never" and mode ~= "copy") or who == "" then
        return nil
    end
    local of = mode == "copy" and trim(r.of) or ""
    local low = who:lower()
    return { mode = mode, who = who, of = of, source = r.source or "window", exact = not low:find("*", 1, true) and low or nil,
        pattern = low:find("*", 1, true) and glob(low) or nil }
end

local function name_matches(rule, names)
    for _, n in ipairs(names) do
        if n ~= "" and ((rule.exact and n == rule.exact) or (rule.pattern and n:find(rule.pattern))) then
            return true
        end
    end
    return false
end

-- The names a rule can use for an actor: identity, engine name, class name, shown name (lowercased).
local function actor_names(actor)
    local out, seen = {}, {}
    local function add(v)
        if type(v) == "string" and v ~= "" and v ~= "?" then
            local l = v:lower()
            if not seen[l] then
                seen[l] = true
                out[#out + 1] = l
            end
        end
    end
    if ctx.identity then
        local ok, id = pcall(ctx.identity, actor)
        add(ok and id or nil)
    end
    add(ctx.name_of(actor))
    if ctx.class_name then
        local ok, c = pcall(ctx.class_name, actor)
        add(ok and c or nil)
    end
    if ctx.label_of then
        local ok, l = pcall(ctx.label_of, actor)
        add(ok and l or nil)
    end
    return out
end

-- The first rule that names this actor (rules are kept most important first), or nil.
local function rule_for(actor)
    if not actor or #rules == 0 then
        return nil
    end
    local names = actor_names(actor)
    for _, r in ipairs(rules) do
        if name_matches(r, names) then
            return r
        end
    end
    return nil
end

-- The edited character a "copy <who> <of>" rule means (its actor key), or nil when it is not being sculpted.
local function key_for(of)
    local low = string.lower(of)
    local probe = { exact = not low:find("*", 1, true) and low or nil, pattern = low:find("*", 1, true) and glob(low) or nil }
    local best = nil
    for key, info in pairs(keyInfo) do
        if name_matches(probe, info.names) and (best == nil or key < best) then
            best = key
        end
    end
    return best
end

-- How the window and the rules name an actor: its identity, else its engine name.
local function who_of(actor)
    if ctx.identity then
        local ok, id = pcall(ctx.identity, actor)
        if ok and type(id) == "string" and id ~= "" then
            return id
        end
    end
    return ctx.name_of(actor)
end

local function shown_name(actor)
    if ctx.label_of then
        local ok, l = pcall(ctx.label_of, actor)
        if ok and type(l) == "string" and l ~= "" then
            return l
        end
    end
    return ctx.name_of(actor)
end

local function turn_down(owner, ownerAddr, ofLabel, why)
    local k = ownerAddr or 0
    if rejected[k] then
        return
    end
    rejectedOrder = rejectedOrder + 1
    rejected[k] = { who = owner and who_of(owner) or "?", label = owner and shown_name(owner) or "?", of = ofLabel or "?", why = why, order = rejectedOrder }
    reportDirty = true
    -- only the newest few are kept
    local n, oldest, oldestKey = 0, nil, nil
    for a, r in pairs(rejected) do
        n = n + 1
        if oldest == nil or r.order < oldest then
            oldest, oldestKey = r.order, a
        end
    end
    if n > MAX_REJECTED and oldestKey then
        rejected[oldestKey] = nil
    end
    ctx.chatter("%s is not a scene copy of %s: %s", owner and ctx.name_of(owner) or "?", ofLabel or "?", why)
end

-- ------------------------------------------------------------- matching

-- Morph targets that say nothing about who a body belongs to: expressions, eyes, speech, correctives, jiggle
-- and the ones animations open and close. Those differ between a character and its copy whenever they play
-- different animations.
local NOT_SHAPE = { "jiggle", "crctve", "correct", "jcm", "look", "eye", "blink", "brow", "lid", "speech", "viseme", "exp_",
    "expr", "emot", "oral", "mouth", "jaw", "lip", "tongue", "teeth", "cheek", "smile", "frown", "sneer", "nose", "gape", "flash",
    "bulge", "open", "close", "breath", "flex", "fcl_", "face", "erect", "squeeze", "suck", "kiss", "swallow", "pose" }

local function shape_morph(name)
    local n = string.lower(name)
    for _, w in ipairs(NOT_SHAPE) do
        if n:find(w, 1, true) then
            return false
        end
    end
    -- in / out pairs that animations drive (Morph_PussyIn, Morph_AnalOut), but not "Thin" or "Chin"
    return not (name:find("In$") or name:find("Out$") or n:find("_in$") or n:find("_out$"))
end

local function is_pawn(actor)
    return actor ~= nil and ctx.is_pawn ~= nil and ctx.is_pawn(actor) == true
end

-- Nil when the candidate's main mesh `cand` looks like character `key`'s main mesh (or there is nothing to
-- compare), else why not: the body morph that differs most.
local MORPH_GAP = 0.1
local function looks_different(key, cand)
    local m = mainComp[key]
    if not m or not ctx.morph_value or not ctx.still(m.comp, m.full) then
        return nil
    end
    local worst, worstName, mine, theirs = 0, nil, 0, 0
    for _, name in ipairs(m.names) do
        if shape_morph(name) and not (ctx.morph_overridden and ctx.morph_overridden(m.comp, name)) then
            local a = ctx.morph_value(m.comp, name)
            local b = a and ctx.morph_value(cand, name)
            if a and b and math.abs(a - b) > worst then
                worst, worstName, mine, theirs = math.abs(a - b), name, a, b
            end
        end
    end
    if worst > MORPH_GAP then
        return string.format("%s %.2f there, %.2f on the character", worstName, theirs, mine)
    end
    return nil
end

-- Does this component (and the rest of its actor) look like a copy of an edited character?
-- Adds the actor's matching meshes; returns how many were added.
local function consider(comp)
    if not alive(comp) or ctx.is_default_object(comp) then
        return 0
    end
    local a = addr(comp)
    if not a or tracked[a] or copies[a] then
        return 0
    end
    local asset = ctx.mesh_prop(comp)
    local src = asset and sources[asset]
    if not src and not hasTargeted then
        return 0
    end
    local owner = owner_of(comp)
    local ownerAddr = owner and addr(owner)
    if ownerAddr and planned[ownerAddr] then
        return 0
    end
    -- rules (window, console, _scene_copies.txt, config.lua, game profile) win over the guesses below
    local rule = owner and rule_for(owner) or nil
    if rule and rule.mode == "never" then
        if src then
            turn_down(owner, ownerAddr, src.ownerLabel or src.key, "marked as not a copy (" .. rule.source .. ")")
        end
        return 0
    end
    local list, shared = {}, {}
    local forced = rule ~= nil
    if rule and rule.of ~= "" then
        -- a copy of a named character: every skeletal mesh on the actor, shared assets or not
        local key = key_for(rule.of)
        local info = key and keyInfo[key]
        if not info or not owner then
            return 0 -- that character is not being sculpted right now
        end
        local target = { key = key, label = info.label, ownerLabel = info.label, ownerEngine = info.engine }
        for _, c in ipairs(skeletal_comps(owner)) do
            local m = ctx.mesh_prop(c)
            if m then
                local s = sources[m]
                list[#list + 1] = { comp = c, asset = m, src = (s and s.key == key) and s or target, forced = true }
            end
        end
        if #list == 0 then
            return 0
        end
        src = list[1].src
    else
        if not src then
            return 0
        end
        -- the actor's meshes that use this character's sculpted assets
        if owner then
            for _, c in ipairs(skeletal_comps(owner)) do
                local m = ctx.mesh_prop(c)
                local s = m and sources[m]
                if s and s.key == src.key then
                    list[#list + 1] = { comp = c, asset = m, src = s }
                    shared[m] = true
                end
            end
        end
        if #list == 0 then
            list[1] = { comp = comp, asset = asset, src = src } -- the actor can't be asked: this mesh alone
            shared[asset] = true
        end
    end
    if not forced then
        local n = 0
        for _ in pairs(shared) do
            n = n + 1
        end
        local need = math.min(2, keyAssets[src.key] or 1)
        if ctx.match == "mesh" then
            need = 1
        end
        local who = owner and controlled(owner) or 0
        if who == 2 then
            return 0 -- another player's character (or the player's own pawn, not being edited)
        end
        if who == 1 then
            need = 2 -- an NPC: one shared mesh is not enough to call it a copy
        end
        if n < need then
            return 0
        end
        -- the body: without the character's main mesh it is an NPC wearing the same eyes / hair / outfit piece
        local main = mainAsset[src.key]
        if ctx.match ~= "mesh" and main and not shared[main] then
            return 0
        end
        -- a pawn with the character's body and outfit: an NPC built from the same parts, unless its body has the same shape
        if ctx.match ~= "mesh" and main and is_pawn(owner) then
            for _, e in ipairs(list) do
                if e.asset == main then
                    local why = looks_different(src.key, e.comp)
                    if why then
                        turn_down(owner, ownerAddr, src.ownerLabel or src.key, "its body differs (" .. why .. ")")
                        return 0
                    end
                    break
                end
            end
        end
    end
    local added = 0
    for _, e in ipairs(list) do
        local ca = addr(e.comp)
        -- the property read says it uses that asset: the engine must agree
        local real = ctx.mesh_asset_addr and ctx.mesh_asset_addr(e.comp)
        if ctx.mesh_asset_addr and real ~= e.asset then
            ca = nil
        end
        if ca and not tracked[ca] and not copies[ca] then
            local rig = ctx.build(e.comp, e.src, owner)
            if rig then
                order = order + 1
                copies[ca] = { comp = e.comp, full = ctx.full_name(e.comp), asset = e.asset, key = e.src.key, rig = rig, owner = ownerAddr, order = order,
                    forced = e.forced == true, ruled = forced, who = owner and who_of(owner) or "?", ownerLabel = owner and shown_name(owner) or "?",
                    ofLabel = e.src.ownerLabel or e.src.key }
                added = added + 1
                changed = true
            end
        end
    end
    if added > 0 then
        rejected[ownerAddr or 0] = nil
        reportDirty = true
        stats.found = stats.found + added
        ctx.say("scene copy of %s found: %d mesh(es) on %s%s", src.ownerLabel or src.key, added, owner and ctx.name_of(owner) or "?",
            forced and (" (rule: " .. rule.mode .. " " .. rule.who .. (rule.of ~= "" and (" of " .. rule.of) or "") .. ", " .. rule.source .. ")") or "")
    end
    return added
end

-- Actors a "copy <who> <of>" rule names, among all skeletal meshes (only while such a rule exists).
local function ruled_owner(comp, checked)
    local owner = owner_of(comp)
    local oa = owner and addr(owner)
    if not oa then
        return false
    end
    if checked[oa] == nil then
        local r = rule_for(owner)
        checked[oa] = r ~= nil and r.mode == "copy" and r.of ~= ""
    end
    return checked[oa]
end

local function search(reason)
    if next(sources) == nil and not hasTargeted then
        return
    end
    local t0 = os.clock()
    lastSearch = ctx.now_ms()
    stats.searches = stats.searches + 1
    stats.lastReason = reason
    local found = 0
    local checked = {}
    for _, comp in ipairs(ctx.find_all() or {}) do
        local asset = ctx.mesh_prop(comp)
        if (asset and sources[asset]) or (hasTargeted and ruled_owner(comp, checked)) then
            found = found + consider(comp)
        end
    end
    stats.ms = (os.clock() - t0) * 1000.0
    ctx.chatter("scene copy search (%s): %d new, %.1f ms", reason, found, stats.ms)
end

local function burst(now, reason)
    if next(sources) == nil and not hasTargeted then
        return
    end
    for _, d in ipairs(BURST) do
        searches[#searches + 1] = { at = now + d, reason = reason }
    end
end

-- ------------------------------------------------------------- watching the camera / rendering

local function view_target(now)
    if unsupported.view and now < unsupported.view then
        return nil
    end
    local pc = ctx.controller and ctx.controller()
    if not pc then
        return nil
    end
    local ok, vt = pcall(function()
        return pc:GetViewTarget()
    end)
    if not ok then
        local ok2, vt2 = pcall(function()
            return pc.PlayerCameraManager.ViewTarget.Target
        end)
        if not ok2 then
            unsupported.view = now + RETRY_MS
            return nil
        end
        vt = vt2
    end
    unsupported.view = nil
    return alive(vt) and addr(vt) or nil
end

local function rendered(now)
    if (unsupported.rendered and now < unsupported.rendered) or not primary or not ctx.still(primary.comp, primary.full) then
        return nil
    end
    local ok, r = pcall(function()
        return primary.comp:WasRecentlyRendered(0.5)
    end)
    if not ok or type(r) ~= "boolean" then
        unsupported.rendered = now + RETRY_MS
        return nil
    end
    unsupported.rendered = nil
    return r
end

local function check_view(now)
    local vt = view_target(now)
    if vt and lastView and vt ~= lastView then
        burst(now, "camera moved to another actor")
    end
    lastView = vt or lastView
    local r = rendered(now)
    if r == false and wasRendered ~= false then
        burst(now, "character out of view")
        away.on, away.count, away.due = true, 0, now + BURST[#BURST] + AWAY_EVERY_MS
    elseif r ~= false then
        away.on = false
    end
    if r ~= nil then
        wasRendered = r
    end
    if away.on and away.due and now >= away.due and away.count < AWAY_MAX then
        away.count = away.count + 1
        away.due = now + AWAY_EVERY_MS
        searches[#searches + 1] = { at = now, reason = "character still out of view" }
    end
end

-- ------------------------------------------------------------- API

function C.setup(c)
    ctx = c
    ctx.match = (type(c.match) == "string" and c.match:lower() == "mesh") and "mesh" or "outfit"
end

-- The meshes sculpted as part of characters (main.lua's rigs before copies are added) and the
-- characters being sculpted (actor address -> true). Copies whose source went away are dropped.
function C.set_sources(rigs, plannedAddr)
    sources, keyAssets, tracked, planned, mainAsset, mainComp, keyInfo = {}, {}, {}, {}, {}, {}, {}
    primary = nil
    local perKey = {}
    local sig = {}
    for _, r in ipairs(rigs) do
        if not r.copy then
            local ca = addr(r.comp)
            if ca then
                tracked[ca] = true
            end
            if r.meshAddr then
                if not sources[r.meshAddr] then
                    sources[r.meshAddr] = { key = r.actorKey, label = r.label, ownerLabel = r.ownerLabel, ownerEngine = r.ownerEngine }
                    perKey[r.actorKey] = (perKey[r.actorKey] or 0) + 1
                    sig[#sig + 1] = tostring(r.meshAddr) .. "=" .. r.actorKey
                end
            end
            if r.primary and not primary then
                primary = { comp = r.comp, full = r.full }
            end
            if r.actorKey and not keyInfo[r.actorKey] then
                local names, seen = {}, {}
                local function add(v)
                    if type(v) == "string" and v ~= "" and not seen[v:lower()] then
                        seen[v:lower()] = true
                        names[#names + 1] = v:lower()
                    end
                end
                add(r.actorKey)
                if ctx.key_identity then
                    local ok, id = pcall(ctx.key_identity, r.actorKey)
                    add(ok and id or nil)
                end
                add(r.ownerLabel)
                add(r.ownerEngine)
                keyInfo[r.actorKey] = { names = names, label = r.ownerLabel or r.actorKey, engine = r.ownerEngine }
            end
            if r.primary and r.meshAddr and not mainAsset[r.actorKey] then
                mainAsset[r.actorKey] = r.meshAddr
                local names = {}
                for n in tostring(r.morphs or ""):gmatch("[^\t]+") do
                    if #names < 256 then
                        names[#names + 1] = n
                    end
                end
                mainComp[r.actorKey] = { comp = r.comp, full = r.full, names = names }
            end
        end
    end
    keyAssets = perKey
    for a in pairs(plannedAddr or {}) do
        planned[a] = true
    end
    table.sort(sig)
    sourceSig = table.concat(sig, ",")
    for a, c in pairs(copies) do
        local s = sources[c.asset]
        if tracked[a] then
            drop(a, "now sculpted as a character")
        elseif c.forced then
            if not keyInfo[c.key] then
                drop(a, "its character is no longer sculpted")
            end
        elseif not s or s.key ~= c.key then
            drop(a, "its character no longer has that mesh")
        elseif c.owner and planned[c.owner] then
            drop(a, "its actor is now sculpted as a character")
        end
    end
end

-- A component that may be a copy (new, or just given a mesh): looked at a few times.
function C.offer(comp)
    if comp == nil or #pending >= MAX_PENDING or (next(sources) == nil and not hasTargeted) then
        return
    end
    local now = ctx.now_ms()
    local due = {}
    for i, d in ipairs(NEW_DELAYS) do
        due[i] = now + d
    end
    pending[#pending + 1] = { comp = comp, due = due, step = 0 }
end

-- Something that usually comes with a 3D preview appeared (a scene capture): search soon.
function C.poke(reason)
    burst(ctx.now_ms(), reason or "scene capture")
end

-- Searches the world's skeletal meshes now (console).
function C.search_now()
    search("asked for")
end

-- The rules, most important first (main.lua puts the window's before config.lua's before the game profile's).
-- Every copy is let go and looked for again under the new rules.
function C.set_rules(list)
    local out = {}
    local seen = {}
    for _, r in ipairs(list or {}) do
        local c = compile_rule(r)
        local k = c and (c.who:lower() .. "|" .. c.source)
        if c and not seen[k] then
            seen[k] = true
            out[#out + 1] = c
        end
    end
    -- exact names before wildcards within a source; sources keep their order
    local rank = {}
    for i, r in ipairs(out) do
        rank[r] = i
    end
    local sourceRank = {}
    for _, r in ipairs(out) do
        sourceRank[r.source] = sourceRank[r.source] or rank[r]
    end
    table.sort(out, function(a, b)
        local sa, sb = sourceRank[a.source], sourceRank[b.source]
        if sa ~= sb then
            return sa < sb
        end
        if (a.exact ~= nil) ~= (b.exact ~= nil) then
            return a.exact ~= nil
        end
        return rank[a] < rank[b]
    end)
    rules = out
    hasTargeted = false
    for _, r in ipairs(rules) do
        hasTargeted = hasTargeted or (r.mode == "copy" and r.of ~= "")
    end
    for a in pairs(copies) do
        drop(a, "rules changed")
    end
    rejected = {}
    reportDirty = true
    if ctx then
        burst(ctx.now_ms(), "rules changed")
    end
end

function C.rules()
    return rules
end

-- For the window (bridge_in.txt): the copies now (one line per actor), the actors turned down lately, the rules.
--   scopy <tab> <who> <tab> <shown name> <tab> <copy of: key> <tab> <copy of: shown name> <tab> <meshes> <tab> <1 = by a rule>
--   snot  <tab> <who> <tab> <shown name> <tab> <of: shown name> <tab> <why>
--   srule <tab> <never|copy> <tab> <who> <tab> <of> <tab> <source>
function C.report()
    local lines = {}
    local byOwner, owners = {}, {}
    for _, c in ipairs(copyList) do
        if copies[addr(c.comp) or 0] == c then
            local k = tostring(c.owner or c.who)
            if not byOwner[k] then
                byOwner[k] = { who = c.who, label = c.ownerLabel, key = c.key, of = c.ofLabel, n = 0, ruled = c.ruled }
                owners[#owners + 1] = k
            end
            byOwner[k].n = byOwner[k].n + 1
        end
    end
    local function clean(v)
        return (tostring(v or ""):gsub("[\t\r\n]", " "))
    end
    for _, k in ipairs(owners) do
        local o = byOwner[k]
        lines[#lines + 1] = table.concat({ "scopy", clean(o.who), clean(o.label), clean(o.key), clean(o.of), tostring(o.n), o.ruled and "1" or "0" }, "\t")
    end
    local rej = {}
    for _, r in pairs(rejected) do
        rej[#rej + 1] = r
    end
    table.sort(rej, function(a, b)
        return a.order > b.order
    end)
    for _, r in ipairs(rej) do
        lines[#lines + 1] = table.concat({ "snot", clean(r.who), clean(r.label), clean(r.of), clean(r.why) }, "\t")
    end
    for _, r in ipairs(rules) do
        lines[#lines + 1] = table.concat({ "srule", r.mode, clean(r.who), clean(r.of), clean(r.source) }, "\t")
    end
    return table.concat(lines, "\n")
end

-- True once after the report changed (main.lua then rewrites bridge_in.txt).
function C.report_dirty()
    local d = reportDirty
    reportDirty = false
    return d
end

-- Runs every tick. Returns true when the copies changed (main.lua then rebuilds the mesh list).
function C.tick(now)
    -- copies still there and still the same mesh
    for a, c in pairs(copies) do
        if not ctx.still(c.comp, c.full) then
            drop(a, "gone")
        elseif ctx.mesh_prop(c.comp) ~= c.asset then
            drop(a, "mesh changed")
        end
    end
    if next(sources) == nil and not hasTargeted then
        pending, searches = {}, {}
    else
        -- new components, once their mesh is likely set
        if #pending > 0 then
            local keep = {}
            for _, p in ipairs(pending) do
                local due = p.due[p.step + 1]
                if due and now >= due then
                    p.step = p.step + 1
                    local done = not alive(p.comp)
                    if not done then
                        local ok, n = pcall(consider, p.comp)
                        done = (ok and n > 0) or copies[addr(p.comp) or 0] ~= nil or tracked[addr(p.comp) or 0] == true
                    end
                    if not done and p.due[p.step + 1] then
                        keep[#keep + 1] = p
                    end
                else
                    keep[#keep + 1] = p
                end
            end
            pending = keep
        end
        if now >= nextCheck then
            nextCheck = now + CHECK_MS
            check_view(now)
        end
        if #searches > 0 then
            local keep, run = {}, nil
            for _, s in ipairs(searches) do
                if now >= s.at then
                    run = run or s.reason
                else
                    keep[#keep + 1] = s
                end
            end
            searches = keep
            if run and now - lastSearch >= MIN_GAP_MS then
                search(run)
            end
        end
    end
    local c = changed
    changed = false
    if c then
        sort_copies()
    end
    return c
end

-- The copies as rigs for main.lua (sent to the DLL after the characters' own meshes).
function C.rigs()
    local out = {}
    for _, c in ipairs(copyList) do
        if copies[addr(c.comp) or 0] == c then
            out[#out + 1] = c.rig
        end
    end
    return out
end

function C.status()
    local text = string.format("scene copies: %d mesh(es) now, %d found, %d search(es)%s%s", #C.rigs(), stats.found, stats.searches,
        stats.searches > 0 and string.format(" (last: %s, %.1f ms)", stats.lastReason, stats.ms) or "",
        next(sources) == nil and "; no sculpted meshes to look for" or "")
    for _, c in ipairs(copyList) do
        if copies[addr(c.comp) or 0] == c then
            text = text .. string.format("\n  %s on %s (%s) -> %s%s", c.rig and c.rig.engine or "?", c.ownerLabel or "?", c.who or "?", c.ofLabel or c.key,
                c.ruled and " [rule]" or "")
        end
    end
    for _, r in pairs(rejected) do
        text = text .. string.format("\n  not a copy: %s (%s): %s", r.label, r.who, r.why)
    end
    if #rules == 0 then
        text = text .. "\n  no rules (ubs copies never <who> | copy <who> [of <character>] | forget <who>)"
    end
    for _, r in ipairs(rules) do
        text = text .. string.format("\n  rule: %s %s%s (%s)", r.mode, r.who, r.of ~= "" and (" of " .. r.of) or "", r.source)
    end
    return text
end

-- Tests
C._debug = function()
    return { sources = sources, mainAsset = mainAsset, copies = copies, pending = pending, searches = searches, away = away, unsupported = unsupported, sig = sourceSig,
        rules = rules, rejected = rejected, keyInfo = keyInfo }
end

return C
