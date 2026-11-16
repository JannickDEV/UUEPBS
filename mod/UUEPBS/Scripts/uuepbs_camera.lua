-- UUEPBS - Character Scale > Camera: moves the game's camera for the character you play.
--
-- Height scales the character from the feet, but the game's camera keeps its own pivot, so
-- a taller character is framed too low. The DLL sends one "cam" line per character that has
-- Camera sliders (bridge.hpp):
--     cam <tab> <actor key> <tab> <lift cm> <tab> <distance> <tab> <follow 0/1> <tab> <height>
-- and this script moves the camera of the controlled pawn when its key has one. It tries, in order:
--   1. the spring arm (camera boom) the active camera hangs on: TargetOffset.Z for the height
--      (world up, so it never tilts the view) and TargetArmLength for the distance;
--   2. the camera component itself (no spring arm): its relative location, Z up and X/Y out;
--   3. the pawn's BaseEyeHeight (games that use the default view point, no camera component).
-- "Follow height" adds (Height - 1) x the pivot's height above the feet, and scales the arm by
-- Height, so the character is framed as before. A camera attached to the mesh (first person,
-- on the head bone) already moves with the scaled pose, so it only gets the manual offset.
-- "Collision follows Height" (uuepbs_collision.lua) moves the capsule, and with it a camera boom,
-- camera or eye height hanging on it, up by what the capsule grew; Follow height takes that off
-- its own lift (and off the measured pivot), so the camera is not raised twice.
--
-- The game may change these values itself (zoom, aiming, crouching). A value that moves away
-- from what we wrote, outside the range between the game's value and ours, is taken as the
-- game's new value and the offset goes on top of it. A value easing back from ours towards the
-- game's own is not (that would grow the offset every tick). When the sliders are reset, the
-- window is switched off or the pawn changes, the game's values are written back.
local M = {}

local ctx = {}
local wanted = {} -- actor key -> { lift, dist, follow, height }
local wantedText = ""
local cur = nil -- the camera we moved: { kind, obj, pawn, pawnAddr, key, chans, pivot, onMesh, label }
local note = "camera: nothing to do (Character Scale > Camera)"
local EPS = 0.01
local MAX_LIFT = 400

local function alive(obj)
    if ctx.alive then
        return ctx.alive(obj)
    end
    local ok, v = pcall(function()
        return obj ~= nil and obj:IsValid()
    end)
    return ok and v == true
end

local function addr(obj)
    if not obj then
        return nil
    end
    if ctx.address_num then
        return ctx.address_num(obj)
    end
    local ok, v = pcall(function()
        return obj:GetAddress()
    end)
    return ok and v or nil
end

local function name(obj)
    if ctx.name_of then
        return ctx.name_of(obj)
    end
    local ok, v = pcall(function()
        return obj:GetFName():ToString()
    end)
    return ok and v or "?"
end

local function chatter(fmt, ...)
    if ctx.chatter then
        ctx.chatter(fmt, ...)
    end
end

local function num(v)
    return type(v) == "number" and v == v and v or nil
end

local function read_num(obj, prop)
    local ok, v = pcall(function()
        return obj[prop]
    end)
    return ok and num(v) or nil
end

local function read_vec(obj, prop)
    local ok, x, y, z = pcall(function()
        local v = obj[prop]
        return v.X, v.Y, v.Z
    end)
    if ok and num(x) and num(y) and num(z) then
        return x, y, z
    end
    return nil
end

-- Struct members are written in place (UE4SS struct wrappers); a whole table is the fallback.
local function write_vec_prop(obj, prop, x, y, z)
    pcall(function()
        local v = obj[prop]
        v.X, v.Y, v.Z = x, y, z
    end)
    local _, _, nz = read_vec(obj, prop)
    if not nz or math.abs(nz - z) > EPS then
        pcall(function()
            obj[prop] = { X = x, Y = y, Z = z }
        end)
    end
end

local function world_z(comp)
    local ok, z = pcall(function()
        return comp:K2_GetComponentLocation().Z
    end)
    return ok and num(z) or nil
end

local function components(actor, path)
    local out = {}
    if not actor or not ctx.engine_class then
        return out
    end
    local cls = ctx.engine_class(path)
    if not cls then
        return out
    end
    pcall(function()
        -- used right away: a later engine call may reuse the returned array's memory
        local list = actor:K2_GetComponentsByClass(cls)
        if ctx.each_of then
            ctx.each_of(list, function(c)
                out[#out + 1] = c
            end)
        else
            for _, c in ipairs(list) do
                out[#out + 1] = c
            end
        end
    end)
    local live = {}
    for _, c in ipairs(out) do
        if alive(c) then
            live[#live + 1] = c
        end
    end
    return live
end

local function is_active(comp)
    local ok, v = pcall(function()
        return comp:IsActive()
    end)
    return not ok or v ~= false
end

local function attach_parent(comp)
    local ok, p = pcall(function()
        return comp:GetAttachParent()
    end)
    return ok and alive(p) and p or nil
end

local function is_mesh(comp)
    local ok, n = pcall(function()
        return comp:GetNumBones()
    end)
    return ok and num(n) and n > 0 or false
end

local function view_target()
    local pc = ctx.controller and ctx.controller()
    if not pc then
        return nil
    end
    local ok, t = pcall(function()
        return pc:GetViewTarget()
    end)
    return ok and alive(t) and t or nil
end

---------------------------------------------------------------------------
-- Channels: one number the game owns, with our offset on top.
local function new_chan(now)
    return { base = now, last = nil }
end

-- `now` is what the property holds this tick. Returns the game's own value.
local function track(ch, now)
    if ch.last == nil or math.abs(now - ch.last) <= EPS then
        return ch.base
    end
    local lo, hi = math.min(ch.base, ch.last), math.max(ch.base, ch.last)
    if now >= lo - EPS and now <= hi + EPS then
        return ch.base -- easing from our value back to the game's: keep the game's value
    end
    ch.base = now -- the game set a new value
    return ch.base
end

---------------------------------------------------------------------------
-- The camera rig kinds.
local Kinds = {}

Kinds.arm = {
    read = function(c)
        local _, _, z = read_vec(c.obj, "TargetOffset")
        return { lift = z, len = read_num(c.obj, "TargetArmLength") }
    end,
    write = function(c, want)
        if want.lift and c.chans.lift then
            local x, y = read_vec(c.obj, "TargetOffset")
            write_vec_prop(c.obj, "TargetOffset", x or 0, y or 0, want.lift)
        end
        if want.len and c.chans.len then
            pcall(function()
                c.obj.TargetArmLength = want.len
            end)
        end
    end,
    pivot = function(c, feet)
        local z = world_z(c.obj)
        return z and feet and (z + (c.chans.lift and c.chans.lift.base or 0) - feet) or nil
    end,
}

Kinds.camera = {
    read = function(c)
        local x, y, z = read_vec(c.obj, "RelativeLocation")
        return { lift = z, x = x, y = y }
    end,
    write = function(c, want)
        local x, y, z = read_vec(c.obj, "RelativeLocation")
        if not z then
            return
        end
        x, y, z = want.x or x, want.y or y, want.lift or z
        local ok = pcall(function()
            c.obj:K2_SetRelativeLocation({ X = x, Y = y, Z = z }, false, {}, false)
        end)
        local _, _, nz = read_vec(c.obj, "RelativeLocation")
        if not ok or not nz or math.abs(nz - z) > EPS then
            write_vec_prop(c.obj, "RelativeLocation", x, y, z)
        end
    end,
    pivot = function(c, feet)
        local z = world_z(c.obj)
        local ch = c.chans.lift
        local moved = ch and ch.last and (ch.last - ch.base) or 0
        return z and feet and (z - moved - feet) or nil
    end,
}

Kinds.eyes = {
    read = function(c)
        return { lift = read_num(c.obj, "BaseEyeHeight") }
    end,
    write = function(c, want)
        if want.lift then
            pcall(function()
                c.obj.BaseEyeHeight = want.lift
            end)
        end
    end,
    pivot = function(c, feet)
        local ok, z = pcall(function()
            return c.obj:K2_GetActorLocation().Z
        end)
        z = ok and num(z) or nil
        return z and feet and (z + (c.chans.lift and c.chans.lift.base or 0) - feet) or nil
    end,
}

-- How far the collision script raised the pawn (its capsule centre), cm.
local function raised(pawn)
    local r = ctx.raised and num(ctx.raised(pawn)) or 0
    return r or 0
end

local function feet_of(pawn, key)
    local z = ctx.feet_z and ctx.feet_z(pawn, key)
    if num(z) then
        return z
    end
    local ok, m = pcall(function()
        return pawn.Mesh
    end)
    if ok and alive(m) then
        return world_z(m)
    end
    return nil
end

-- Finds what to move on the controlled pawn (or the view target the controller looks through).
local function capture(pawn, key)
    local holders = { pawn }
    local vt = view_target()
    if vt and addr(vt) ~= addr(pawn) then
        holders[#holders + 1] = vt
    end
    local kind, obj, onMesh
    for _, actor in ipairs(holders) do
        local arms = components(actor, "/Script/Engine.SpringArmComponent")
        local cams = components(actor, "/Script/Engine.CameraComponent")
        -- the active camera's own spring arm first
        table.sort(cams, function(a, b)
            return (is_active(a) and 1 or 0) > (is_active(b) and 1 or 0)
        end)
        for _, cam in ipairs(cams) do
            local p = attach_parent(cam)
            for _, arm in ipairs(arms) do
                if p and addr(p) == addr(arm) then
                    kind, obj = "arm", arm
                    break
                end
            end
            if kind then
                break
            end
        end
        if not kind and arms[1] and read_num(arms[1], "TargetArmLength") then
            kind, obj = "arm", arms[1]
        end
        if not kind and cams[1] then
            kind, obj = "camera", cams[1]
            local p = attach_parent(obj)
            onMesh = p ~= nil and is_mesh(p)
        end
        if kind then
            break
        end
    end
    if not kind and read_num(pawn, "BaseEyeHeight") then
        kind, obj = "eyes", pawn
    end
    if not kind then
        return nil
    end
    local c = { kind = kind, obj = obj, pawn = pawn, pawnAddr = addr(pawn), key = key, chans = {}, onMesh = onMesh or false,
        label = name(obj) }
    local now = Kinds[kind].read(c)
    for ch, v in pairs(now) do
        if v then
            c.chans[ch] = new_chan(v)
        end
    end
    if not c.chans.lift then
        return nil
    end
    c.pivot = Kinds[kind].pivot(c, feet_of(pawn, key))
    if c.pivot and not onMesh then
        c.pivot = c.pivot - raised(pawn) -- measured where the game's own capsule would have it
    end
    if not c.pivot or c.pivot <= 1 then
        c.pivot = 160 -- feet unknown: a person-sized guess
    end
    chatter("camera: moving %s (%s) of %s, pivot %.0f cm above the feet", c.label, kind, name(pawn), c.pivot)
    return c
end

local function restore()
    if cur and alive(cur.obj) then
        local back = {}
        for ch, st in pairs(cur.chans) do
            back[ch] = st.base
        end
        pcall(Kinds[cur.kind].write, cur, back)
        chatter("camera: %s back to the game's values", cur.label)
    end
    cur = nil
end

local function clamp(v, lo, hi)
    return math.max(lo, math.min(hi, v))
end

local function apply(c, w)
    local now = Kinds[c.kind].read(c)
    local base = {}
    local rebased = false
    for ch, st in pairs(c.chans) do
        if now[ch] then
            local before = st.base
            base[ch] = track(st, now[ch])
            rebased = rebased or base[ch] ~= before
        end
    end
    if rebased and c.kind ~= "arm" then
        -- the game moved the camera / eye height: measure the pivot again
        local p = Kinds[c.kind].pivot(c, feet_of(c.pawn, c.key))
        p = p and not c.onMesh and p - raised(c.pawn) or p
        if p and p > 1 then
            c.pivot = p
        end
    end
    local h = w.follow and w.height or 1
    -- (the capsule, and the camera on it, may already be raised by the collision script)
    local followLift = (w.follow and not c.onMesh) and (h - 1) * c.pivot - raised(c.pawn) or 0
    local lift = clamp(followLift + w.lift, -MAX_LIFT, MAX_LIFT)
    local dist = w.dist * (w.follow and h or 1)
    local want = {}
    if base.lift then
        want.lift = base.lift + lift
    end
    if base.len then
        want.len = math.max(0, base.len * dist)
    end
    if base.x and base.y then
        want.x, want.y = base.x * dist, base.y * dist
    end
    local dirty = false
    for ch, v in pairs(want) do
        if not now[ch] or math.abs(now[ch] - v) > EPS then
            dirty = true
        end
    end
    if dirty then
        Kinds[c.kind].write(c, want)
    end
    for ch, v in pairs(want) do
        if c.chans[ch] then
            c.chans[ch].last = v
        end
    end
    local what = c.kind == "arm" and "camera boom" or c.kind == "camera" and "camera" or "eye height"
    note = string.format("camera: %s %s of %s, %+.0f cm%s%s", what, c.label, name(c.pawn), lift,
        (base.len or base.x) and string.format(", distance %.2fx", dist) or ", distance not adjustable here",
        c.onMesh and " (on the mesh: follows the pose by itself)" or "")
end

---------------------------------------------------------------------------
-- API used by main.lua

function M.setup(c)
    ctx = c or {}
end

-- "<lift>\t<distance>\t<follow>\t<height>" (the rest of a cam line) -> settings, nil when broken.
function M.parse(fields)
    local l, d, f, h = (fields or ""):match("^([^\t]*)\t([^\t]*)\t([^\t]*)\t([^\t]*)")
    l, d, h = tonumber(l), tonumber(d), tonumber(h)
    if not (l and d and h) or d <= 0 or h <= 0 then
        return nil
    end
    return { lift = l, dist = d, follow = f == "1", height = h }
end

-- key -> raw fields, from every cam line of one bridge_out read.
function M.want(raw)
    local keys, parts = {}, {}
    for k in pairs(raw or {}) do
        keys[#keys + 1] = k
    end
    table.sort(keys)
    local list = {}
    for _, k in ipairs(keys) do
        local s = M.parse(raw[k])
        if s then
            list[k] = s
            parts[#parts + 1] = k .. "=" .. raw[k]
        end
    end
    local text = table.concat(parts, "\n")
    if text ~= wantedText then
        wantedText = text
        wanted = list
        return true
    end
    return false
end

-- Anything to do this tick (cheap: the fast loop calls this first).
function M.busy()
    return cur ~= nil or next(wanted) ~= nil
end

function M.tick()
    if not M.busy() then
        return
    end
    local pawn = ctx.pawn and ctx.pawn()
    local key = pawn and ctx.key_of and ctx.key_of(pawn)
    local w = key and wanted[key]
    if cur and (not w or not alive(cur.obj) or not pawn or cur.pawnAddr ~= addr(pawn)) then
        restore()
    end
    if not w then
        note = pawn and "camera: the game's own (no Camera sliders on this character)" or "camera: no controlled character"
        return
    end
    if not cur then
        cur = capture(pawn, key)
        if not cur then
            note = "camera: no spring arm, camera component or eye height found on " .. name(pawn)
            return
        end
    end
    apply(cur, w)
end

-- Puts the game's values back (the mod is unloaded or switched off).
function M.reset()
    restore()
end

function M.status()
    return note
end

return M
