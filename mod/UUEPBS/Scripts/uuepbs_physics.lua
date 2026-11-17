-- UUEPBS - jiggle physics that follows the sliders
--
-- Games make breasts, butt, belly and genitals bounce with physics nodes in the character's
-- Animation Blueprint: Rigid Body (with the mesh's Physics Asset), AnimDynamics, Kawaii Physics,
-- Spring Bone, Trail. These nodes run while the game animates the character, BEFORE the sliders
-- edit the pose, so they bounce and collide at the original sizes: an enlarged chest goes through
-- the arms, a longer penis swings like the short one.
--
-- Three modes, chosen in the window (Character Scale > Overall, "Jiggle physics") or with
-- "ubs physics follow|off|keep", remembered in the presets folder (_physics.txt):
--   follow  the physics shapes of a character whose sliders scale bones are scaled with them:
--           Physics Asset bodies (spheres, capsules, boxes) and their joint positions and limits,
--           AnimDynamics boxes and limits, Kawaii Physics radii and collision limits. Sizes are
--           relative to the whole body, so Height alone changes nothing. The nodes are restarted
--           once to pick the new sizes up (the post-process blueprint at once, the main blueprint
--           when no montage plays, so a running scene is not cut).
--   off     the physics nodes of an edited character are switched off (their Alpha set to 0) and
--           its cloth simulation is paused, so nothing bounces at the wrong size.
--   keep    the game's own physics, untouched.
-- Whatever is changed is put back when the character stops scaling bones (sliders back to 1,
-- Before, sliders off) or the mode changes.
--
-- "Heavier when bigger" (follow mode; the window's slider next to the choice, "ubs physics weight
-- <percent>", config.lua PhysicsWeight): a bigger breast, butt or anything else with physics also
-- moves like a heavier one. The springs that pull it back to the animated pose are softened and their
-- damping with them, so it sways slower and wider, sags more and settles later; a smaller one gets
-- quicker and tighter. It is the same rule for every node type (Kawaii Stiffness/Damping, AnimDynamics
-- spring constants, Spring Bone stiffness/damping, Physics Asset joint limits and drives), so it works
-- in any game without per-game lists: only bones whose size the sliders change are touched.
-- The model is a scaled-up soft body: its mass grows with the volume (size^3) but its springiness
-- only with its cross-section, so the spring per mass falls with size^2. At 100% a breast twice as
-- big gets a quarter of the spring (half the bounce frequency); 200% is twice as pronounced, 0% off.
--
-- A Physics Asset is shared by every character using the same body. Only bodies whose bones the
-- sliders scale are changed, the engine's own collision bodies keep theirs until the game rebuilds
-- them, and when two edited characters share an asset the first one decides its sizes.

local P = {}

local ctx = { say = function() end, fname = function(n) return n end }
local mode = "follow"
local weight = 1.0 -- "Heavier when bigger" strength: 0 off, 1 natural, up to MAX_WEIGHT
local tracked = {} -- component address -> state (see track())
local assets = {}  -- physics asset address -> { asset, owner = comp address, saved = {...}, factors key }
local told = {}    -- messages said once

local NODE_KINDS = {
    -- struct name part (lower case) -> kind
    { "rigidbody", "rigidbody" },
    { "animdynamics", "dynamics" },
    { "kawaii", "kawaii" },
    { "springbone", "spring" },
    { "spring_bone", "spring" },
    { "trail", "trail" },
    { "jiggle", "jiggle" },
    { "physicscontrol", "rigidbody" },
}

-- Node fields scaled in "follow" mode. bone = the node's own bone; arrays list per-element bones.
-- kind "len" = a length (radius...), "vec" = an offset in the bone's space.
-- weight = fields that make the node heavier when its bone grows ("Heavier when bigger"):
-- "stiff" = a spring (divided by the weight), "damp" = its damping (divided by the weight's square root,
-- so it bounces as many times as before, only slower).
local NODE_FIELDS = {
    dynamics = {
        bone = "BoundBone",
        fields = { { { "BoxExtents" }, "vec" }, { { "LocalJointOffset" }, "vec" }, { { "SphereCollisionRadius" }, "len" } },
        arrays = { { "SphericalLimits", "DrivingBone", { { { "SphereLocalOffset" }, "vec" }, { { "LimitRadius" }, "len" } } } },
        weight = { { { "LinearSpringConstant" }, "stiff" }, { { "AngularSpringConstant" }, "stiff" } },
    },
    kawaii = {
        bone = "RootBone",
        fields = { { { "PhysicsSettings", "Radius" }, "len" }, { { "DummyBoneLength" }, "len" } },
        arrays = {
            { "SphericalLimits", "DrivingBone", { { { "OffsetLocation" }, "vec" }, { { "Radius" }, "len" } } },
            { "CapsuleLimits", "DrivingBone", { { { "OffsetLocation" }, "vec" }, { { "Radius" }, "len" }, { { "Length" }, "len" } } },
            { "BoxLimits", "DrivingBone", { { { "OffsetLocation" }, "vec" }, { { "Extent" }, "vec" } } },
        },
        weight = { { { "PhysicsSettings", "Stiffness" }, "stiff" }, { { "PhysicsSettings", "Damping" }, "damp" } },
    },
    spring = {
        bone = "SpringBone",
        fields = {},
        weight = { { { "SpringStiffness" }, "stiff" }, { { "SpringDamping" }, "damp" } },
    },
}

-- Physics Asset joint springs ("Heavier when bigger"), on joints whose child bone (ConstraintBone1) grew:
-- soft limits and motor drives of the constraint profile.
local JOINT_SPRINGS = {}
for _, p in ipairs({ { "ConeLimit" }, { "TwistLimit" }, { "LinearLimit" }, { "AngularDrive", "SlerpDrive" },
    { "AngularDrive", "TwistDrive" }, { "AngularDrive", "SwingDrive" }, { "LinearDrive", "XDrive" }, { "LinearDrive", "YDrive" },
    { "LinearDrive", "ZDrive" } }) do
    for _, f in ipairs({ { "Stiffness", "stiff" }, { "Damping", "damp" } }) do
        local path = { "ProfileInstance" }
        for _, k in ipairs(p) do
            path[#path + 1] = k
        end
        path[#path + 1] = f[1]
        JOINT_SPRINGS[#JOINT_SPRINGS + 1] = { path, f[2] }
    end
end

local MAX_WEIGHT = 3.0   -- 300%
local HEAVY_MIN, HEAVY_MAX = 0.04, 50 -- the weight factor stays in here, so huge or tiny sliders keep the simulation stable

local EPS = 0.02 -- scale changes below 2% are ignored
local CHECK_MS = 1000 -- bone scales are read this often per character
local REINIT_MS = 3000 -- a blueprint is restarted at most this often

---------------------------------------------------------------------------
-- helpers
---------------------------------------------------------------------------
local function say(fmt, ...)
    ctx.say(fmt, ...)
end

local function say_once(id, fmt, ...)
    if not told[id] then
        told[id] = true
        say(fmt, ...)
    end
end

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

local function str(v)
    if v == nil then
        return nil
    end
    if type(v) == "string" then
        return v
    end
    local ok, s = pcall(function()
        return v:ToString()
    end)
    return ok and s or nil
end

local function name_of(o)
    local ok, n = pcall(function()
        return o:GetFName():ToString()
    end)
    return ok and n or "?"
end

local function get(obj, path)
    local ok, v = pcall(function()
        local cur = obj
        for _, k in ipairs(path) do
            cur = cur[k]
            if cur == nil then
                return nil
            end
        end
        return cur
    end)
    return ok and v or nil
end

local function set(obj, path, value)
    return pcall(function()
        local cur = obj
        for i = 1, #path - 1 do
            cur = cur[path[i]]
        end
        cur[path[#path]] = value
    end)
end

-- The elements of a TArray (UE4SS) or a plain Lua list (tests), 1-based.
local function items(arr)
    local out = {}
    if arr == nil then
        return out
    end
    local okN, n = pcall(function()
        return arr:GetArrayNum()
    end)
    if okN and type(n) == "number" then
        for i = 1, n do
            local ok, v = pcall(function()
                return arr[i]
            end)
            if ok and v ~= nil then
                out[#out + 1] = v
            end
        end
        return out
    end
    if type(arr) == "table" then
        for i, v in ipairs(arr) do
            out[i] = v
        end
    end
    return out
end

local function vec(v)
    if v == nil then
        return nil
    end
    local ok, x, y, z = pcall(function()
        return v.X, v.Y, v.Z
    end)
    if ok and type(x) == "number" then
        return { x, y, z }
    end
    return nil
end

local function set_vec(obj, path, xyz)
    local ok = true
    for i, axis in ipairs({ "X", "Y", "Z" }) do
        local p = { table.unpack(path) }
        p[#p + 1] = axis
        ok = set(obj, p, xyz[i]) and ok
    end
    return ok
end

local function mean3(s)
    return (math.abs(s[1] * s[2] * s[3])) ^ (1 / 3)
end

local function is_one(s)
    return math.abs(s[1] - 1) < EPS and math.abs(s[2] - 1) < EPS and math.abs(s[3] - 1) < EPS
end

local function same_scale(a, b)
    if a == nil or b == nil then
        return a == b
    end
    return math.abs(a[1] - b[1]) < EPS and math.abs(a[2] - b[2]) < EPS and math.abs(a[3] - b[3]) < EPS
end

-- Axes of an FRotator (degrees) as UE's FRotationMatrix rows: X, Y, Z.
function P.rotator_axes(rot)
    local p = math.rad(rot and rot[1] or 0)
    local y = math.rad(rot and rot[2] or 0)
    local r = math.rad(rot and rot[3] or 0)
    local sp, cp, sy, cy, sr, cr = math.sin(p), math.cos(p), math.sin(y), math.cos(y), math.sin(r), math.cos(r)
    return { cp * cy, cp * sy, sp }, { sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp },
        { -(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp }
end

-- How much a bone-space direction `a` grows under per-axis scale s.
function P.axis_factor(s, a)
    return math.sqrt((s[1] * a[1]) ^ 2 + (s[2] * a[2]) ^ 2 + (s[3] * a[3]) ^ 2)
end

local function rotator(v)
    if v == nil then
        return nil
    end
    local ok, p, y, r = pcall(function()
        return v.Pitch, v.Yaw, v.Roll
    end)
    if ok and type(p) == "number" then
        return { p, y, r }
    end
    return nil
end

---------------------------------------------------------------------------
-- finding the physics nodes of a mesh's animation blueprints
---------------------------------------------------------------------------
local function node_kind(struct_name)
    local n = string.lower(struct_name or "")
    if not n:find("animnode", 1, true) and not n:find("kawaii", 1, true) then
        return nil
    end
    for _, k in ipairs(NODE_KINDS) do
        if n:find(k[1], 1, true) then
            return k[2]
        end
    end
    return nil
end

local function struct_name_of(prop)
    local ok, n = pcall(function()
        return prop:GetStruct():GetFName():ToString()
    end)
    return ok and n or nil
end

local function is_engine_class(cls)
    local ok, full = pcall(function()
        return cls:GetFullName()
    end)
    if not ok or type(full) ~= "string" then
        return false
    end
    return full:find(" /Script/Engine.", 1, true) ~= nil or full:find(" /Script/CoreUObject.", 1, true) ~= nil
end

-- { { inst, prop = name, struct = name, kind = kind }, ... } for one anim instance.
function P.find_nodes(inst)
    local out = {}
    if not alive(inst) then
        return out
    end
    local okC, cls = pcall(function()
        return inst:GetClass()
    end)
    local depth = 0
    while okC and cls and depth < 16 and not is_engine_class(cls) do
        pcall(function()
            cls:ForEachProperty(function(prop)
                local sn = struct_name_of(prop)
                local kind = sn and node_kind(sn)
                if kind then
                    out[#out + 1] = { inst = inst, prop = prop:GetFName():ToString(), struct = sn, kind = kind }
                end
            end)
        end)
        local okS, super = pcall(function()
            return cls:GetSuperStruct()
        end)
        cls = okS and alive(super) and super or nil
        depth = depth + 1
    end
    return out
end

local function anim_instances(comp)
    local main, post
    pcall(function()
        main = comp:GetAnimInstance()
    end)
    pcall(function()
        post = comp:GetPostProcessInstance()
    end)
    return alive(main) and main or nil, alive(post) and post or nil
end

local function node_of(n)
    local ok, v = pcall(function()
        return n.inst[n.prop]
    end)
    return ok and v or nil
end

---------------------------------------------------------------------------
-- bone scales (relative to the whole body, read from the edited pose)
---------------------------------------------------------------------------
local function cs_scale(comp, bone)
    local ok, s = pcall(function()
        local t = comp:GetSocketTransform(ctx.fname(bone), 2) -- RTS_Component
        local v = t.Scale3D
        return { v.X, v.Y, v.Z }
    end)
    if ok and type(s) == "table" and type(s[1]) == "number" then
        return s
    end
    return nil
end

local function root_name(comp)
    local ok, n = pcall(function()
        return comp:GetBoneName(0)
    end)
    return ok and str(n) or nil
end

-- bone -> relative scale {x,y,z} for the bones asked for (missing = 1).
local function read_scales(comp, bones)
    local root = root_name(comp)
    local rs = root and cs_scale(comp, root) or { 1, 1, 1 }
    if rs[1] == 0 or rs[2] == 0 or rs[3] == 0 then
        rs = { 1, 1, 1 }
    end
    local out = {}
    for bone in pairs(bones) do
        local s = cs_scale(comp, bone)
        if s then
            out[bone] = { s[1] / rs[1], s[2] / rs[2], s[3] / rs[3] }
        end
    end
    return out
end

---------------------------------------------------------------------------
-- "follow": node fields and physics asset geometry
---------------------------------------------------------------------------
local function bone_of(v)
    local b = v and get(v, { "BoneName" })
    return str(b)
end

-- Every value this module changes is remembered as { obj, path, kind ("num"/"vec"), original }.
local function remember(list, obj, path, kind)
    local v = get(obj, path)
    if kind == "vec" then
        v = vec(v)
    elseif type(v) ~= "number" then
        v = nil
    end
    if v ~= nil then
        list[#list + 1] = { obj = obj, path = path, kind = kind, value = v }
        return list[#list]
    end
    return nil
end

local function write(entry, factor)
    if entry.kind == "vec" then
        local f = type(factor) == "table" and factor or { factor, factor, factor }
        return set_vec(entry.obj, entry.path, { entry.value[1] * f[1], entry.value[2] * f[2], entry.value[3] * f[3] })
    end
    return set(entry.obj, entry.path, entry.value * factor)
end

local function put_back(list)
    for i = #list, 1, -1 do
        local e = list[i]
        if e.kind == "vec" then
            set_vec(e.obj, e.path, e.value)
        else
            set(e.obj, e.path, e.value)
        end
    end
end

-- Node fields: collects { entry, bone, how } for one node.
local function node_targets(n)
    local spec = NODE_FIELDS[n.kind]
    local node = spec and node_of(n)
    local out = {}
    if not node then
        return out
    end
    local own = bone_of(get(node, { spec.bone }))
    for _, f in ipairs(spec.fields) do
        out[#out + 1] = { obj = node, path = f[1], how = f[2], bone = own }
    end
    if weight > 0 then
        for _, f in ipairs(spec.weight or {}) do
            out[#out + 1] = { obj = node, path = f[1], how = f[2], bone = own }
        end
    end
    for _, a in ipairs(spec.arrays or {}) do
        for _, el in ipairs(items(get(node, { a[1] }))) do
            local b = bone_of(get(el, { a[2] })) or own
            for _, f in ipairs(a[3]) do
                out[#out + 1] = { obj = el, path = f[1], how = f[2], bone = b }
            end
        end
    end
    return out
end

-- Physics asset shapes and joints: { obj, path, how, bone, axis = {..}, axes = {...} }.
local SHAPES = {
    { "SphereElems", { { { "Radius" }, "mean" } } },
    { "SphylElems", { { { "Radius" }, "across" }, { { "Length" }, "along" } } },
    { "TaperedCapsuleElems", { { { "Radius0" }, "across" }, { { "Radius1" }, "across" }, { { "Length" }, "along" } } },
    { "BoxElems", { { { "X" }, "ax1" }, { { "Y" }, "ax2" }, { { "Z" }, "ax3" } } },
}

local function asset_targets(asset, scaled)
    local out = {}
    for _, setup in ipairs(items(get(asset, { "SkeletalBodySetups" }))) do
        local bone = str(get(setup, { "BoneName" }))
        if bone and scaled[bone] then
            for _, shape in ipairs(SHAPES) do
                for _, el in ipairs(items(get(setup, { "AggGeom", shape[1] }))) do
                    local ax, ay, az = P.rotator_axes(rotator(get(el, { "Rotation" })))
                    out[#out + 1] = { obj = el, path = { "Center" }, how = "vec", bone = bone }
                    for _, f in ipairs(shape[2]) do
                        out[#out + 1] = { obj = el, path = f[1], how = f[2], bone = bone, axes = { ax, ay, az } }
                    end
                end
            end
        end
    end
    for _, c in ipairs(items(get(asset, { "ConstraintSetup" }))) do
        local inst = get(c, { "DefaultInstance" })
        local b1 = inst and str(get(inst, { "ConstraintBone1" }))
        local b2 = inst and str(get(inst, { "ConstraintBone2" }))
        if b1 and scaled[b1] then
            out[#out + 1] = { obj = inst, path = { "Pos1" }, how = "vec", bone = b1 }
            out[#out + 1] = { obj = inst, path = { "ProfileInstance", "LinearLimit", "Limit" }, how = "mean", bone = b1 }
            if weight > 0 then
                for _, f in ipairs(JOINT_SPRINGS) do
                    out[#out + 1] = { obj = inst, path = f[1], how = f[2], bone = b1 }
                end
            end
        end
        if b2 and scaled[b2] then
            out[#out + 1] = { obj = inst, path = { "Pos2" }, how = "vec", bone = b2 }
        end
    end
    return out
end

-- How much heavier a bone of relative scale s moves: (size^2)^weight, kept between HEAVY_MIN and HEAVY_MAX.
function P.heaviness(s, w)
    w = w or weight
    local h = (mean3(s) ^ 2) ^ w
    if h ~= h then
        return 1
    end
    return math.max(HEAVY_MIN, math.min(HEAVY_MAX, h))
end

local function factor_for(t, s)
    if t.how == "stiff" then
        return 1 / P.heaviness(s)
    elseif t.how == "damp" then
        return 1 / math.sqrt(P.heaviness(s))
    elseif t.how == "vec" then
        return s
    elseif t.how == "along" then
        return P.axis_factor(s, t.axes[3])
    elseif t.how == "across" then
        return (P.axis_factor(s, t.axes[1]) + P.axis_factor(s, t.axes[2])) / 2
    elseif t.how == "ax1" then
        return P.axis_factor(s, t.axes[1])
    elseif t.how == "ax2" then
        return P.axis_factor(s, t.axes[2])
    elseif t.how == "ax3" then
        return P.axis_factor(s, t.axes[3])
    end
    return mean3(s) -- "len", "mean"
end

-- Writes targets scaled by their bone's scale; remembers originals in `saved` (once per value).
local function scale_targets(targets, scales, saved, index)
    local n = 0
    for _, t in ipairs(targets) do
        local s = t.bone and scales[t.bone]
        local id = tostring(address(t.obj) or t.obj) .. "/" .. table.concat(t.path, ".")
        local e = index[id]
        if not e and s and not is_one(s) then
            e = remember(saved, t.obj, t.path, (t.how == "vec") and "vec" or "num")
            index[id] = e or false
        end
        if e then
            if write(e, s and factor_for(t, s) or 1) then
                n = n + 1
            end
        end
    end
    return n
end

---------------------------------------------------------------------------
-- restarting the nodes so they build their bodies again
---------------------------------------------------------------------------
local function restart_post(comp)
    return pcall(function()
        if comp.bDisablePostProcessBlueprint then
            return -- the game has it off
        end
        comp:SetDisablePostProcessBlueprint(true)
        comp:SetDisablePostProcessBlueprint(false) -- re-initializes the post-process instance
    end)
end

local function restart_main(comp)
    return pcall(function()
        local allowed = not comp.bDisableRigidBodyAnimNode
        comp:SetAllowRigidBodyAnimNode(not allowed, false)
        comp:SetAllowRigidBodyAnimNode(allowed, true) -- re-initializes the animation
    end)
end

local function montage_playing(inst)
    local ok, v = pcall(function()
        return inst:IsAnyMontagePlaying()
    end)
    return ok and v == true
end

-- Restarts what holds changed nodes. `now` forces the main blueprint even during a montage.
local function restart(st, force, t)
    if not st.restart_main and not st.restart_post then
        return
    end
    if not force and st.restarted and t - st.restarted < REINIT_MS then
        return
    end
    if st.restart_post then
        local ok = restart_post(st.comp)
        st.restart_post = false
        if not ok then
            say_once("post" .. st.label, "physics: could not restart the post-process blueprint of %s", st.label)
        end
    end
    if st.restart_main then
        if not force and st.main and montage_playing(st.main) then
            if not st.waiting then
                st.waiting = true
                say("physics: %s plays an animation; its physics picks up the new sizes when it ends", st.label)
            end
            return
        end
        st.waiting = false
        st.restart_main = false
        if not restart_main(st.comp) then
            say_once("main" .. st.label, "physics: could not restart the animation blueprint of %s", st.label)
        end
    end
    st.restarted = t
end

---------------------------------------------------------------------------
-- "off": Alpha 0 on the physics nodes, cloth paused
---------------------------------------------------------------------------
local function switch_off(st)
    local n = 0
    for _, nd in ipairs(st.nodes) do
        local node = node_of(nd)
        if node and not nd.off then
            local alpha = get(node, { "Alpha" })
            local bool = get(node, { "bAlphaBoolEnabled" })
            if type(alpha) == "number" then
                nd.off = { alpha = alpha, bool = bool }
                set(node, { "Alpha" }, 0)
                if bool ~= nil then
                    set(node, { "bAlphaBoolEnabled" }, false)
                end
                if get(node, { "Alpha" }) == 0 then
                    n = n + 1
                else
                    say_once("alpha" .. nd.prop, "physics: could not switch %s off on %s", nd.prop, st.label)
                end
            end
        end
    end
    if not st.cloth then
        local okS, was = pcall(function()
            return st.comp:IsClothingSimulationSuspended()
        end)
        if not (okS and was == true) and pcall(function() st.comp:SuspendClothingSimulation() end) then
            st.cloth = true
        end
    end
    return n
end

local function switch_on(st)
    for _, nd in ipairs(st.nodes) do
        local node = nd.off and node_of(nd)
        if node and get(node, { "Alpha" }) == 0 then
            set(node, { "Alpha" }, nd.off.alpha)
            if nd.off.bool ~= nil then
                set(node, { "bAlphaBoolEnabled" }, nd.off.bool)
            end
        end
        nd.off = nil
    end
    if st.cloth then
        pcall(function()
            st.comp:ResumeClothingSimulation()
        end)
        st.cloth = nil
    end
end

---------------------------------------------------------------------------
-- per mesh
---------------------------------------------------------------------------
local function physics_asset(comp)
    local a = get(comp, { "PhysicsAssetOverride" })
    if alive(a) then
        return a
    end
    local mesh
    for _, f in ipairs({ "GetSkinnedAsset", "GetSkeletalMeshAsset" }) do
        local ok, m = pcall(function()
            return comp[f](comp)
        end)
        if ok and alive(m) then
            mesh = m
            break
        end
    end
    mesh = mesh or get(comp, { "SkeletalMesh" })
    if not alive(mesh) then
        return nil
    end
    local ok, pa = pcall(function()
        return mesh:GetPhysicsAsset()
    end)
    if ok and alive(pa) then
        return pa
    end
    pa = get(mesh, { "PhysicsAsset" })
    return alive(pa) and pa or nil
end

local function track(r)
    local a = address(r.comp)
    local st = a and tracked[a]
    local main, post = anim_instances(r.comp)
    -- Compare by address: UE4SS hands out a new Lua object for every engine call, and two of them for the same
    -- UObject are not ==. (Comparing the objects re-tracked every mesh on every pass and restarted its animation
    -- and physics blueprints about twice a second.)
    if st and address(st.main) == address(main) and address(st.post) == address(post) then
        st.comp, st.main, st.post = r.comp, main, post
        for _, nd in ipairs(st.nodes) do
            nd.inst = nd.post and post or main
        end
        return st
    end
    if st then
        P.release(a)
    end
    st = { comp = r.comp, key = r.actorKey, label = r.label or name_of(r.comp), main = main, post = post, nodes = {},
        saved = {}, index = {}, applied = nil, due = 0, assets = {} }
    -- (not ipairs({ main, post }): with no main instance - a hair or outfit mesh that only has a post-process
    -- blueprint - ipairs would stop at the nil and miss the post-process nodes)
    for _, pair in ipairs({ { main, false }, { post, true } }) do
        if pair[1] then
            for _, nd in ipairs(P.find_nodes(pair[1])) do
                nd.post = pair[2]
                st.nodes[#st.nodes + 1] = nd
            end
        end
    end
    st.rigid = false
    for _, nd in ipairs(st.nodes) do
        if nd.kind == "rigidbody" then
            st.rigid = true
            local pa = get(node_of(nd) or {}, { "OverridePhysicsAsset" })
            if alive(pa) then
                st.assets[#st.assets + 1] = pa
            end
        end
    end
    if st.rigid then
        local pa = physics_asset(r.comp)
        if pa then
            st.assets[#st.assets + 1] = pa
        end
    end
    if a then
        tracked[a] = st
    end
    if #st.nodes > 0 then
        local kinds = {}
        for _, nd in ipairs(st.nodes) do
            kinds[#kinds + 1] = nd.struct .. (nd.post and " (post-process)" or "")
        end
        say("physics: %s has %s", st.label, table.concat(kinds, ", "))
    end
    return st
end

-- bones whose scale matters for this mesh
local function wanted_bones(st)
    local bones = {}
    for _, nd in ipairs(st.nodes) do
        for _, t in ipairs(node_targets(nd)) do
            if t.bone then
                bones[t.bone] = true
            end
        end
    end
    for _, pa in ipairs(st.assets) do
        for _, setup in ipairs(items(get(pa, { "SkeletalBodySetups" }))) do
            local b = str(get(setup, { "BoneName" }))
            if b then
                bones[b] = true
            end
        end
    end
    return bones
end

local function scales_key(scales)
    local parts = {}
    for b, s in pairs(scales) do
        if not is_one(s) then
            parts[#parts + 1] = string.format("%s=%.2f,%.2f,%.2f", b, s[1], s[2], s[3])
        end
    end
    table.sort(parts)
    return table.concat(parts, ";")
end

local function unfollow(st)
    if #st.saved > 0 then
        put_back(st.saved)
        st.saved, st.index = {}, {}
        st.restart_main = st.restart_main or st.main_changed
        st.restart_post = st.restart_post or st.post_changed
    end
    for _, pa in ipairs(st.assets) do
        local aa = address(pa)
        local rec = aa and assets[aa]
        if rec and rec.owner == address(st.comp) then
            put_back(rec.saved)
            assets[aa] = nil
            if st.rigid then
                for _, nd in ipairs(st.nodes) do
                    if nd.kind == "rigidbody" then
                        st.restart_main = st.restart_main or not nd.post
                        st.restart_post = st.restart_post or nd.post
                    end
                end
            end
        end
    end
    st.applied = nil
    st.counts = nil
end

local function follow(st, t, force)
    if not force and t < st.due then
        return
    end
    st.due = t + CHECK_MS
    local scales = read_scales(st.comp, wanted_bones(st))
    local sizes = scales_key(scales)
    local key = sizes .. "|" .. string.format("%.2f", weight)
    if key == st.applied then
        return
    end
    -- node fields (per character, nothing shared)
    put_back(st.saved)
    st.saved, st.index = {}, {}
    local nodeCount, bodyCount = 0, 0
    st.main_changed, st.post_changed = false, false
    for _, nd in ipairs(st.nodes) do
        local k = scale_targets(node_targets(nd), scales, st.saved, st.index)
        if k > 0 then
            nodeCount = nodeCount + 1
            st.main_changed = st.main_changed or not nd.post
            st.post_changed = st.post_changed or nd.post
        end
    end
    -- physics asset geometry (shared by everyone with this body)
    local me = address(st.comp)
    for _, pa in ipairs(st.assets) do
        local aa = address(pa) or pa
        local rec = assets[aa]
        if rec and rec.owner ~= me and tracked[rec.owner] then
            say_once("shared" .. tostring(aa) .. st.label, "physics: %s shares its physics asset %s with %s, which sets its sizes",
                st.label, name_of(pa), tracked[rec.owner].label)
        else
            if rec then
                put_back(rec.saved)
            end
            rec = { asset = pa, owner = me, saved = {}, index = {} }
            assets[aa] = rec
            bodyCount = bodyCount + scale_targets(asset_targets(pa, scales), scales, rec.saved, rec.index)
            if st.rigid then
                for _, nd in ipairs(st.nodes) do
                    if nd.kind == "rigidbody" then
                        st.main_changed = st.main_changed or not nd.post
                        st.post_changed = st.post_changed or nd.post
                    end
                end
            end
        end
    end
    st.restart_main = st.restart_main or st.main_changed
    st.restart_post = st.restart_post or st.post_changed
    st.applied = key
    st.counts = { nodes = nodeCount, values = bodyCount }
    if sizes ~= "" then
        say("physics follows the sliders on %s: %d node(s), %d physics asset value(s) scaled%s", st.label, nodeCount, bodyCount,
            weight > 0 and string.format(", %d%% heavier when bigger", math.floor(weight * 100 + 0.5)) or "")
    end
end

-- Puts everything of one mesh back.
function P.release(a)
    local st = tracked[a]
    if not st then
        return
    end
    switch_on(st)
    unfollow(st)
    if alive(st.comp) then
        restart(st, true, math.huge)
    end
    tracked[a] = nil
end

---------------------------------------------------------------------------
-- public
---------------------------------------------------------------------------
function P.setup(c)
    ctx.say = c.say or ctx.say
    ctx.fname = c.fname or ctx.fname
end

function P.mode()
    return mode
end

-- "follow" | "off" | "keep". A change restarts blueprints on the next apply even during a montage.
function P.set_mode(m)
    m = string.lower(tostring(m or ""))
    if m ~= "follow" and m ~= "off" and m ~= "keep" then
        return false
    end
    if m ~= mode then
        mode = m
        P.force = true
    end
    return true
end

function P.weight()
    return weight
end

-- "Heavier when bigger" strength: 0 (off) .. 3 (300%), 1 = natural. Accepts "150%" or "1.5".
function P.set_weight(w)
    local txt = tostring(w or "")
    local n = tonumber((txt:gsub("%%", "")))
    if not n or n ~= n then
        return false
    end
    if txt:find("%", 1, true) or n > MAX_WEIGHT then
        n = n / 100
    end
    n = math.max(0, math.min(MAX_WEIGHT, n))
    if math.abs(n - weight) > 0.001 then
        weight = n
        P.force = true
    end
    return true
end

-- rigs: tracked meshes ({ comp, label, actorKey, ... }); scaled: set of actor keys whose sliders
-- scale bones; t: milliseconds. Called every tick from the game thread.
function P.apply(rigs, scaled, t)
    t = t or 0
    local force = P.force
    P.force = false
    local want = {}
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
    for a in pairs(tracked) do
        if not want[a] then
            P.release(a)
        end
    end
    for _, r in pairs(want) do
        local st = track(r)
        if mode == "off" then
            unfollow(st)
            local n = switch_off(st)
            if n > 0 then
                say("physics off on %s while its sliders scale bones (%d node(s)%s)", st.label, n, st.cloth and ", cloth paused" or "")
            end
        else
            switch_on(st)
            follow(st, t, force)
        end
        restart(st, force, t)
    end
end

function P.restore_all()
    for a in pairs(tracked) do
        P.release(a)
    end
end

-- One line for the window (Character Scale > Overall).
function P.summary()
    if mode == "keep" then
        return "The game's own physics."
    end
    local parts = {}
    for _, st in pairs(tracked) do
        local kinds = {}
        for _, nd in ipairs(st.nodes) do
            kinds[nd.kind] = (kinds[nd.kind] or 0) + 1
        end
        local names = {}
        for k, n in pairs(kinds) do
            local label = ({ rigidbody = "Rigid Body", dynamics = "AnimDynamics", kawaii = "Kawaii", spring = "Spring Bone",
                trail = "Trail", jiggle = "jiggle" })[k] or k
            names[#names + 1] = n .. " " .. label
        end
        table.sort(names)
        if #names == 0 then
            parts[#parts + 1] = st.label .. ": no physics nodes found"
        elseif mode == "off" then
            parts[#parts + 1] = st.label .. ": " .. table.concat(names, ", ") .. " off"
        else
            parts[#parts + 1] = st.label .. ": " .. table.concat(names, ", ") .. (st.waiting and " (after the animation)" or " scaled")
                .. ((weight > 0 and not st.waiting) and ", heavier when bigger" or "")
        end
    end
    table.sort(parts)
    if #parts == 0 then
        return "No character scales bones right now."
    end
    return table.concat(parts, "; ")
end

-- For "ubs physics" / "ubs status".
function P.status()
    return "physics (" .. mode .. (mode == "follow" and string.format(", heavier when bigger %d%%", math.floor(weight * 100 + 0.5)) or "")
        .. "): " .. P.summary()
end

return P
