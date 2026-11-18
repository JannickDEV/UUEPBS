-- UUEPBS - Height done by the engine (Character Scale > Overall > Height).
--
-- The DLL used to do all of Height on the bones, after the game's animation had run. The game's own
-- animation graph then still works with a body of the old size: a head that looks at the camera, a
-- point in front of the eyes or another character aims from where the head was, not from where it is
-- now. With "Collision follows Height" (or Camera > Follow height) the capsule and the camera move up
-- with Height while that old head stays down, so the head tilts up (down when smaller).
--
-- So the engine does Height: the character's meshes get RelativeScale3D times Height, which the
-- animation graph, sockets, look-at targets and root motion all take into account, and the DLL takes
-- that factor off its own bone scale ("escale" lines, bridge.hpp). Meshes attached to another mesh of
-- the character (head, hair, outfit on the body) take the scale on from it and are not scaled again.
--
-- Hand-over without a pop: the bridge files take a few hundred ms each way, so a mesh is never scaled
-- before the DLL has said it takes that factor off ("escaled" lines). Until then the DLL keeps doing
-- all of Height on the bones. Height still being dragged is left to the DLL; the engine follows once
-- the value has been still for SETTLE_MS. Meshes the DLL does not know (portraits) are not touched.
--
-- When Height goes back to 1, the window shows the original (Before), the mod is switched off or a
-- mesh is let go, the game's own scale is written back.
local M = {}

local ctx = {}
local enabled = true
local wanted = {} -- actor key -> Height (from the DLL's "height" lines)
local acks = {} -- address hex -> factor the DLL takes off the bones ("escaled")
local seenAcks = false -- the DLL has sent at least one bridge file since this script started
local settle = {} -- actor key -> { value, since } Height waiting to stand still
local planned = {} -- actor key -> factor the engine should carry
local held = {} -- address hex of a top mesh -> { comp, key, orig = {x,y,z}, written, children = {address hex} }
local reported = {} -- address hex -> factor in our "escale" lines
local note = "engine scale: nothing to do (no character with a Height other than 1)"
local EPS = 0.0005
local SETTLE_MS = 600
local MAX_DEPTH = 8

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
    if ctx.address_num then
        return ctx.address_num(obj)
    end
    local ok, v = pcall(function()
        return obj:GetAddress()
    end)
    return ok and v or nil
end

local function hex(obj)
    local a = addr(obj)
    return a and string.format("%X", a) or nil
end

local function chatter(fmt, ...)
    if ctx.chatter then
        ctx.chatter(fmt, ...)
    end
end

local function num(v)
    return type(v) == "number" and v == v and v or nil
end

local function same(a, b)
    return math.abs(a - b) <= EPS
end

local function read_scale(comp)
    local ok, x, y, z = pcall(function()
        local s = comp.RelativeScale3D
        return s.X, s.Y, s.Z
    end)
    if ok and num(x) and num(y) and num(z) then
        return { x, y, z }
    end
    return nil
end

local function write_scale(comp, s)
    local ok = pcall(function()
        comp:SetRelativeScale3D({ X = s[1], Y = s[2], Z = s[3] })
    end)
    local now = read_scale(comp)
    if not ok or not now or not same(now[3], s[3]) then
        pcall(function()
            local v = comp.RelativeScale3D
            v.X, v.Y, v.Z = s[1], s[2], s[3]
        end)
        now = read_scale(comp)
        if not now or not same(now[3], s[3]) then
            pcall(function()
                comp.RelativeScale3D = { X = s[1], Y = s[2], Z = s[3] }
            end)
            now = read_scale(comp)
        end
    end
    return now ~= nil and same(now[1], s[1]) and same(now[2], s[2]) and same(now[3], s[3])
end

local function attach_parent(comp)
    local ok, p = pcall(function()
        return comp:GetAttachParent()
    end)
    return ok and alive(p) and p or nil
end

-- A mesh whose scale does not follow its parent's (bAbsoluteScale / IsUsingAbsoluteScale).
local function absolute_scale(comp)
    local ok, v = pcall(function()
        return comp:IsUsingAbsoluteScale()
    end)
    if ok and type(v) == "boolean" then
        return v
    end
    ok, v = pcall(function()
        return comp.bAbsoluteScale
    end)
    return ok and v == true
end

local function scaled(orig, f)
    return { orig[1] * f, orig[2] * f, orig[3] * f }
end

local function factor_of(key)
    return planned[key] or 1
end

local function ack_of(a)
    return acks[a] or 1
end

-- The DLL's "height" lines (actor key -> text) and "escaled" lines (address hex -> text).
function M.want(heights, escaled)
    local w = {}
    for key, text in pairs(heights or {}) do
        local h = tonumber(text)
        if h and h > 0 and not same(h, 1) then
            w[key] = h
        end
    end
    wanted = w
    local a = {}
    for address, text in pairs(escaled or {}) do
        local f = tonumber(text)
        if f and f > 0 then
            a[address] = f
        end
    end
    acks = a
    seenAcks = true
end

function M.setup(context)
    ctx = context or {}
end

function M.set_enabled(on)
    enabled = on and true or false
end

function M.enabled()
    return enabled
end

-- Height that stood still for SETTLE_MS becomes the engine's part; the DLL does all of it meanwhile.
local function update_plan(now)
    local changed = false
    for key, h in pairs(enabled and wanted or {}) do
        local s = settle[key]
        if not s or not same(s.value, h) then
            settle[key] = { value = h, since = now }
        elseif now - s.since >= SETTLE_MS and not same(factor_of(key), h) then
            planned[key] = h
            changed = true
        end
    end
    for key in pairs(planned) do
        if not enabled or not wanted[key] then
            -- back to the game's size at once (the DLL does Height = 1 already)
            planned[key] = nil
            changed = true
        end
    end
    for key in pairs(settle) do
        if not enabled or not wanted[key] then
            settle[key] = nil
        end
    end
    return changed
end

-- The character's meshes the DLL knows, sorted into top meshes (scaled here) and meshes hanging on one.
local function sort_rigs(rigs)
    local byAddr = {}
    for _, r in ipairs(rigs or {}) do
        if r.comp and alive(r.comp) and r.actorKey then
            local a = hex(r.comp)
            if a then
                byAddr[a] = r
            end
        end
    end
    local tops, under = {}, {}
    for a, r in pairs(byAddr) do
        local top = nil
        if not absolute_scale(r.comp) then
            local p, depth = attach_parent(r.comp), 0
            while p and depth < MAX_DEPTH do
                local pa = hex(p)
                if pa and byAddr[pa] then
                    top = pa -- keeps going: the highest mesh of the chain is the one scaled
                end
                if absolute_scale(p) then
                    break
                end
                p, depth = attach_parent(p), depth + 1
            end
        end
        if top then
            under[a] = top
        else
            tops[a] = r
        end
    end
    return tops, under
end

-- Game thread. Returns true when the "escale" lines changed (the bridge file needs writing).
function M.tick(rigs, now)
    now = now or 0
    update_plan(now)
    local tops, under = sort_rigs(rigs)

    -- Meshes held but no longer listed (or no longer a top mesh): the game's scale back.
    for a, st in pairs(held) do
        if not tops[a] or not alive(st.comp) then
            if alive(st.comp) and st.written ~= 1 then
                write_scale(st.comp, st.orig)
            end
            held[a] = nil
        end
    end

    -- What each mesh should carry, and what we tell the DLL.
    local report = {}
    for a, r in pairs(tops) do
        local want = factor_of(r.actorKey)
        local st = held[a]
        if not same(want, 1) or (st and st.written ~= 1) then
            report[a] = want
        end
    end
    for a, top in pairs(under) do
        local r = tops[top]
        if r and report[top] then
            report[a] = report[top]
        end
    end

    -- Scale (or unscale) a top mesh once the DLL takes that factor off it and every mesh hanging on it.
    if seenAcks then
        for a, r in pairs(tops) do
            local want = report[a] or 1
            local st = held[a]
            local current = st and st.written or 1
            if not same(want, current) then
                local ready = same(ack_of(a), want)
                for c, top in pairs(under) do
                    if top == a and not same(ack_of(c), want) then
                        ready = false
                    end
                end
                if ready then
                    if not st then
                        local s = read_scale(r.comp)
                        if s then
                            st = { comp = r.comp, key = r.actorKey, orig = s, written = 1 }
                            held[a] = st
                        end
                    else
                        -- the game set a scale of its own meanwhile: that is its new size
                        local s = read_scale(r.comp)
                        if s and not same(s[3], st.orig[3] * st.written) then
                            st.orig, st.written = s, 1
                            chatter("engine scale: %s changed its own scale, kept as the game's", tostring(r.label or a))
                        end
                    end
                    if st and write_scale(r.comp, scaled(st.orig, want)) then
                        st.written = want
                        if same(want, 1) then
                            held[a] = nil
                        end
                    end
                end
            elseif st and not same(want, 1) then
                -- the game put its own scale back (a respawned mesh, a cutscene): ours again
                local s = read_scale(r.comp)
                if s and not same(s[3], st.orig[3] * st.written) then
                    st.orig = s
                    st.written = 1
                end
            end
        end
    end

    -- Keep reporting a mesh until it is back to the game's size.
    for a, st in pairs(held) do
        if not report[a] and st.written ~= 1 then
            report[a] = 1
        end
    end

    local changed = false
    for a, f in pairs(report) do
        if not reported[a] or not same(reported[a], f) then
            changed = true
        end
    end
    for a in pairs(reported) do
        if not report[a] then
            changed = true
        end
    end
    reported = report

    local n, keys = 0, {}
    for _, st in pairs(held) do
        n = n + 1
        keys[st.key] = true
    end
    if not enabled then
        note = "engine scale: off (HeightByEngine = false in config.lua), Height is done on the bones"
    elseif n == 0 then
        note = next(wanted) and "engine scale: waiting for Height to stand still and the DLL to take it off the bones"
            or "engine scale: nothing to do (no character with a Height other than 1)"
    else
        local parts = {}
        for a, st in pairs(held) do
            parts[#parts + 1] = string.format("%s x%.3f", a, st.written)
        end
        table.sort(parts)
        note = "engine scale: " .. table.concat(parts, ", ")
    end
    return changed
end

-- "escale" lines for bridge_in.txt.
function M.report()
    local lines = {}
    for a, f in pairs(reported) do
        lines[#lines + 1] = string.format("escale\t%s\t%.4f", a, f)
    end
    table.sort(lines)
    return table.concat(lines, "\n")
end

-- Whether anything is (or is about to be) scaled.
function M.busy()
    return next(wanted) ~= nil or next(held) ~= nil or next(planned) ~= nil
end

-- Puts the game's scale back on every mesh at once (script unloading, everything off).
function M.release()
    for a, st in pairs(held) do
        if alive(st.comp) then
            write_scale(st.comp, st.orig)
        end
        held[a] = nil
    end
    planned, settle, reported = {}, {}, {}
end

function M.summary()
    return note
end

return M
