-- UUEPBS - collision that follows Height (Character Scale > Overall > "Collision follows Height").
--
-- Height scales every mesh of a character from its feet, but the game's collision capsule keeps its
-- size: a 2x character walks through low ceilings and into walls up to its chest, a 0.5x one stops
-- at gaps it would fit through. The DLL sends one "height" line per character whose Height is not 1
-- (bridge.hpp):
--     height <tab> <actor key> <tab> <height>
-- and while the switch is on this script sizes that character's capsule the way the engine's own
-- crouch does, only the other way round:
--   * half height and radius times Height (the half height never below the radius, like UE);
--   * the actor moves up by what the half height grew (down by what it shrank), so the bottom of the
--     capsule, and with it the feet, stays on the floor;
--   * the mesh moves down by the same amount (RelativeLocation Z, and ACharacter's
--     BaseTranslationOffset so the engine's own mesh smoothing agrees), so it stays where it was;
--   * the movement component's MaxStepHeight times Height (a taller character takes higher steps).
-- The mesh's offset from the bottom of the capsule is measured once and kept, which is also what the
-- engine keeps when crouching. Auto Ground (the DLL's pose lift) works inside the mesh and is not
-- touched; Lua-mode Auto Ground adopts the new mesh height as its base and adds its lift on top.
--
-- The game may change the capsule itself (crouching sets the crouched half height and the default
-- radius, standing up sets the defaults back). A value that differs from what we wrote is taken as
-- the game's new size, and Height goes on top of it on the next check (CollisionPollMs).
-- When Height goes back to 1, the switch is turned off, the window shows the original (Before) or
-- the character goes, the game's sizes are written back and the actor moves down again.
--
-- The camera script asks raised(actor) how far the capsule centre was moved, because a camera boom
-- hangs on the capsule and already moved up with it.
local M = {}

local ctx = {}
local wanted = {} -- actor key -> height
local wantedText = ""
local enabled = true
local held = {} -- actor key -> state, see capture()
local note = "collision: nothing to do (no character with a Height other than 1)"
local EPS = 0.01

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

local function read_obj(obj, prop)
    local ok, v = pcall(function()
        return obj[prop]
    end)
    return ok and alive(v) and v or nil
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

local function set_rel_z(comp, z)
    local x, y, oz = read_vec(comp, "RelativeLocation")
    if not oz then
        return false
    end
    local ok = pcall(function()
        comp:K2_SetRelativeLocation({ X = x, Y = y, Z = z }, false, {}, false)
    end)
    local _, _, nz = read_vec(comp, "RelativeLocation")
    if not ok or not nz or math.abs(nz - z) > EPS then
        write_vec_prop(comp, "RelativeLocation", x, y, z)
        _, _, nz = read_vec(comp, "RelativeLocation")
    end
    return nz ~= nil and math.abs(nz - z) <= EPS
end

local function attach_parent(comp)
    local ok, p = pcall(function()
        return comp:GetAttachParent()
    end)
    return ok and alive(p) and p or nil
end

local function root_of(actor)
    local ok, r = pcall(function()
        return actor:K2_GetRootComponent()
    end)
    if ok and alive(r) then
        return r
    end
    return read_obj(actor, "RootComponent")
end

local function is_capsule(comp)
    return comp ~= nil and read_num(comp, "CapsuleHalfHeight") ~= nil and read_num(comp, "CapsuleRadius") ~= nil
end

local function capsule_size(cap)
    return read_num(cap, "CapsuleHalfHeight"), read_num(cap, "CapsuleRadius")
end

local function set_capsule(cap, hh, r)
    local ok = pcall(function()
        cap:SetCapsuleSize(r, hh, true)
    end)
    local nh, nr = capsule_size(cap)
    if not ok or not nh or math.abs(nh - hh) > EPS or math.abs(nr - r) > EPS then
        pcall(function()
            cap.CapsuleRadius = r
            cap.CapsuleHalfHeight = hh
        end)
    end
end

-- Moves what the capsule hangs on up (+) or down (-) by dz, without sweeping: the bottom of the
-- capsule stays where it was, so nothing new is touched.
local function shift(st, dz)
    if math.abs(dz) <= EPS then
        return
    end
    if st.capIsRoot then
        local ok, x, y, z = pcall(function()
            local l = st.actor:K2_GetActorLocation()
            return l.X, l.Y, l.Z
        end)
        if ok and num(z) then
            pcall(function()
                st.actor:K2_SetActorLocation({ X = x, Y = y, Z = z + dz }, false, {}, true)
            end)
        end
    else
        local _, _, z = read_vec(st.cap, "RelativeLocation")
        if z then
            set_rel_z(st.cap, z + dz)
        end
    end
end

-- The mesh keeps its height above the bottom of the capsule (footOff), whatever the half height is.
local function place_mesh(st, hh)
    if not st.mesh or not alive(st.mesh) then
        return
    end
    local z = st.footOff - hh
    set_rel_z(st.mesh, z)
    if st.baseOffset then
        local x, y = read_vec(st.actor, "BaseTranslationOffset")
        if x then
            write_vec_prop(st.actor, "BaseTranslationOffset", x, y, z)
        end
    end
end

-- One number the game owns: `base` is the game's own value, `last` what we wrote.
local function track(ch, now)
    if ch.last == nil or math.abs(now - ch.last) > EPS then
        ch.base = now -- the game set a new value (crouching, standing up, a cutscene...)
    end
    return ch.base
end

local function capture(key, actor, mesh)
    local cap = read_obj(actor, "CapsuleComponent")
    if not is_capsule(cap) then
        cap = root_of(actor)
        if not is_capsule(cap) then
            return nil
        end
    end
    local hh, r = capsule_size(cap)
    local root = root_of(actor)
    local st = { key = key, actor = actor, actorAddr = addr(actor), cap = cap, capIsRoot = root ~= nil and addr(root) == addr(cap),
        hh = { base = hh }, r = { base = r }, label = name(actor) }
    -- The character's own mesh, when it hangs on the capsule (ACharacter: Mesh, attached to the capsule).
    local m = read_obj(actor, "Mesh") or mesh
    local parent = m and attach_parent(m)
    if m and parent and addr(parent) == addr(cap) then
        local _, _, mz = read_vec(m, "RelativeLocation")
        if mz then
            if ctx.mesh_base_z then
                mz = num(ctx.mesh_base_z(m)) or mz -- Lua-mode Auto Ground's lift is not part of the game's offset
            end
            st.mesh, st.footOff = m, mz + hh
            st.baseOffset = read_vec(actor, "BaseTranslationOffset") ~= nil
        end
    end
    local cmc = read_obj(actor, "CharacterMovement")
    local step = cmc and read_num(cmc, "MaxStepHeight")
    if step then
        st.cmc, st.step = cmc, { base = step }
    end
    chatter("collision: %s capsule %s, half height %.1f, radius %.1f%s", st.label, name(cap), hh, r,
        st.mesh and string.format(", mesh %.1f cm above its bottom", st.footOff) or ", no mesh on the capsule")
    return st
end

local function apply(st, h)
    local hh, r = capsule_size(st.cap)
    if not hh then
        return
    end
    local baseH, baseR = track(st.hh, hh), track(st.r, r)
    local wantR = ctx.scale_radius == false and baseR or baseR * h
    local wantH = math.max(baseH * h, wantR)
    if math.abs(hh - wantH) > EPS or math.abs(r - wantR) > EPS then
        set_capsule(st.cap, wantH, wantR)
        shift(st, wantH - hh)
        place_mesh(st, wantH)
    end
    st.hh.last, st.r.last = wantH, wantR
    st.raised = wantH - baseH
    if st.step and ctx.scale_step ~= false and alive(st.cmc) then
        local now = read_num(st.cmc, "MaxStepHeight")
        if now then
            local want = track(st.step, now) * h
            if math.abs(now - want) > EPS then
                pcall(function()
                    st.cmc.MaxStepHeight = want
                end)
            end
            st.step.last = want
        end
    end
    st.note = string.format("%s: capsule %.1f x %.1f cm (game %.1f x %.1f), raised %+.1f cm%s", st.label, wantH, wantR, baseH, baseR, st.raised,
        st.step and string.format(", step %.1f cm", st.step.last or st.step.base) or "")
end

local function restore(st)
    if not alive(st.cap) then
        return
    end
    local hh, r = capsule_size(st.cap)
    if hh and st.hh.last and math.abs(hh - st.hh.last) <= EPS then
        local baseR = (r and st.r.last and math.abs(r - st.r.last) <= EPS) and st.r.base or r
        set_capsule(st.cap, st.hh.base, baseR)
        shift(st, st.hh.base - hh)
        place_mesh(st, st.hh.base)
    end
    if st.step and st.step.last and alive(st.cmc) then
        local now = read_num(st.cmc, "MaxStepHeight")
        if now and math.abs(now - st.step.last) <= EPS then
            pcall(function()
                st.cmc.MaxStepHeight = st.step.base
            end)
        end
    end
    chatter("collision: %s back to the game's capsule", st.label)
end

---------------------------------------------------------------------------
-- API used by main.lua

-- c: alive, address_num, name_of, chatter, actor_of(key) -> actor, mesh; mesh_base_z(mesh);
-- scale_radius / scale_step (false = leave alone).
function M.setup(c)
    ctx = c or {}
end

-- key -> raw height text, from every height line of one bridge_out read. True when it changed.
function M.want(raw)
    local keys, parts, list = {}, {}, {}
    for k in pairs(raw or {}) do
        keys[#keys + 1] = k
    end
    table.sort(keys)
    for _, k in ipairs(keys) do
        local h = tonumber(raw[k])
        if h and h > 0.05 and h < 20 then
            list[k] = h
            parts[#parts + 1] = k .. "=" .. raw[k]
        end
    end
    local text = table.concat(parts, "\n")
    if text ~= wantedText then
        wantedText, wanted = text, list
        return true
    end
    return false
end

function M.set_enabled(on)
    enabled = on and true or false
end

function M.enabled()
    return enabled
end

-- Anything to do this tick (cheap: the loop calls this first).
function M.busy()
    return next(held) ~= nil or (enabled and next(wanted) ~= nil)
end

function M.tick()
    if not M.busy() then
        return
    end
    for key, st in pairs(held) do
        local actor = enabled and wanted[key] and ctx.actor_of and ctx.actor_of(key) or nil
        -- (a character briefly missing from the mesh list while it is re-read keeps its capsule)
        local gone = not enabled or not wanted[key] or not alive(st.cap) or not alive(st.actor)
        if gone or (actor and addr(actor) ~= st.actorAddr) then
            restore(st)
            held[key] = nil
        end
    end
    if not enabled then
        note = "collision: off (the game's own capsules)"
        return
    end
    local lines, missing = {}, 0
    for key, h in pairs(wanted) do
        local st = held[key]
        if not st then
            local actor, mesh = nil, nil
            if ctx.actor_of then
                actor, mesh = ctx.actor_of(key)
            end
            st = actor and alive(actor) and capture(key, actor, mesh) or nil
            held[key] = st
        end
        if st then
            apply(st, h)
            lines[#lines + 1] = "collision: " .. st.note
        else
            missing = missing + 1
        end
    end
    if #lines == 0 then
        note = missing > 0 and "collision: no capsule found on the character (not an ACharacter / capsule root)" or "collision: nothing to do"
    else
        table.sort(lines)
        note = table.concat(lines, "\n") .. (missing > 0 and string.format("\ncollision: %d character(s) without a capsule", missing) or "")
    end
end

-- How far the capsule centre of this actor sits above the game's own (cm, 0 when untouched).
function M.raised(actor)
    local a = addr(actor)
    if not a then
        return 0
    end
    for _, st in pairs(held) do
        if st.actorAddr == a then
            return st.raised or 0
        end
    end
    return 0
end

-- Puts every capsule back (the mod is unloaded or switched off).
function M.reset()
    for key, st in pairs(held) do
        restore(st)
        held[key] = nil
    end
end

function M.status()
    return note
end

return M
