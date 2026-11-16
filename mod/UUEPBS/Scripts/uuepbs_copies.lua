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
--     a body mesh: its meshes include at least two of the character's sculpted mesh assets (one, when
--     the character only has one), and it is not controlled by the player. An AI-controlled actor needs
--     two shared assets even when the character has only one mesh (NPCs often share a body mesh).
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
        ctx.chatter("scene copy %s let go (%s)", c.rig and c.rig.engine or "?", why)
    end
end

-- ------------------------------------------------------------- matching

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
    if not src then
        return 0
    end
    local owner = owner_of(comp)
    local ownerAddr = owner and addr(owner)
    if ownerAddr and planned[ownerAddr] then
        return 0
    end
    -- the actor's meshes that use this character's sculpted assets
    local list, shared = {}, {}
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
    local added = 0
    for _, e in ipairs(list) do
        local ca = addr(e.comp)
        if ca and not tracked[ca] and not copies[ca] then
            local rig = ctx.build(e.comp, e.src, owner)
            if rig then
                order = order + 1
                copies[ca] = { comp = e.comp, full = ctx.full_name(e.comp), asset = e.asset, key = e.src.key, rig = rig, owner = ownerAddr, order = order }
                added = added + 1
                changed = true
            end
        end
    end
    if added > 0 then
        stats.found = stats.found + added
        ctx.say("scene copy of %s found: %d mesh(es) on %s", src.ownerLabel or src.key, added, owner and ctx.name_of(owner) or "?")
    end
    return added
end

local function search(reason)
    if next(sources) == nil then
        return
    end
    local t0 = os.clock()
    lastSearch = ctx.now_ms()
    stats.searches = stats.searches + 1
    stats.lastReason = reason
    local found = 0
    for _, comp in ipairs(ctx.find_all() or {}) do
        local asset = ctx.mesh_prop(comp)
        if asset and sources[asset] then
            found = found + consider(comp)
        end
    end
    stats.ms = (os.clock() - t0) * 1000.0
    ctx.chatter("scene copy search (%s): %d new, %.1f ms", reason, found, stats.ms)
end

local function burst(now, reason)
    if next(sources) == nil then
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
    sources, keyAssets, tracked, planned = {}, {}, {}, {}
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
        elseif not s or s.key ~= c.key then
            drop(a, "its character no longer has that mesh")
        elseif c.owner and planned[c.owner] then
            drop(a, "its actor is now sculpted as a character")
        end
    end
end

-- A component that may be a copy (new, or just given a mesh): looked at a few times.
function C.offer(comp)
    if comp == nil or #pending >= MAX_PENDING or next(sources) == nil then
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
    if next(sources) == nil then
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
    return string.format("scene copies: %d mesh(es) now, %d found, %d search(es)%s%s", #C.rigs(), stats.found, stats.searches,
        stats.searches > 0 and string.format(" (last: %s, %.1f ms)", stats.lastReason, stats.ms) or "",
        next(sources) == nil and "; no sculpted meshes to look for" or "")
end

-- Tests
C._debug = function()
    return { sources = sources, copies = copies, pending = pending, searches = searches, away = away, unsupported = unsupported, sig = sourceSig }
end

return C
