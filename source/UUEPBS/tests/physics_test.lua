-- UUEPBS - jiggle physics (Scripts/uuepbs_physics.lua) against a mocked engine.
--   lua physics_test.lua ../../../mod/UUEPBS/Scripts
-- Checks: physics nodes are found in the main and post-process blueprints, "follow" scales Kawaii /
-- AnimDynamics fields and Physics Asset shapes and joints by the bones' scale relative to the body
-- (Height alone changes nothing), blueprints are restarted (the main one not during a montage),
-- everything goes back when the scaling ends, "off" sets Alpha 0 and pauses cloth and puts it back,
-- "keep" touches nothing, a shared asset is sized by its first character, capsule maths.
local dir = arg[1] or "../../../mod/UUEPBS/Scripts"
package.path = dir .. "/?.lua;" .. package.path

local failures = 0
local function check(cond, what)
    if not cond then
        failures = failures + 1
        print("FAIL: " .. what)
    end
end
local function near(a, b)
    return math.abs(a - b) < 1e-6
end

local nextAddr = 0x1000
local function object(t)
    nextAddr = nextAddr + 0x10
    local addr = nextAddr
    t.alive = true
    t.IsValid = function(self) return self.alive end
    t.GetAddress = function() return addr end
    t.GetFName = function(self)
        return { ToString = function() return self.name or "?" end }
    end
    return t
end
local function fname(s)
    return { ToString = function() return s end }
end
local function v3(x, y, z)
    return { X = x, Y = y, Z = z }
end

-- a class with struct properties { name = struct }
local function class(full, props, super)
    return object({
        GetFullName = function() return full end,
        ForEachProperty = function(_, fn)
            for _, p in ipairs(props) do
                fn({
                    GetFName = function() return fname(p[1]) end,
                    GetStruct = function() return { GetFName = function() return fname(p[2]) end } end,
                })
            end
        end,
        GetSuperStruct = function() return super end,
    })
end
local engineAnim = class("Class /Script/Engine.AnimInstance", {})

-- physics asset: breast body (sphere + capsule along X), spine body, a joint breast -> spine
local breastSphere = { Center = v3(10, 0, 0), Radius = 8, Rotation = { Pitch = 0, Yaw = 0, Roll = 0 } }
local breastCapsule = { Center = v3(0, 0, 0), Radius = 4, Length = 10, Rotation = { Pitch = 90, Yaw = 0, Roll = 0 } } -- capsule Z -> bone X
local spineBox = { Center = v3(0, 0, 0), X = 20, Y = 20, Z = 20, Rotation = { Pitch = 0, Yaw = 0, Roll = 0 } }
local joint = { ConstraintBone1 = fname("breast_l"), ConstraintBone2 = fname("spine_03"), Pos1 = v3(2, 0, 0), Pos2 = v3(5, 5, 0),
    ProfileInstance = { LinearLimit = { Limit = 3 } } }
local asset = object({
    name = "PA_Body",
    SkeletalBodySetups = {
        { BoneName = fname("breast_l"), AggGeom = { SphereElems = { breastSphere }, SphylElems = { breastCapsule } } },
        { BoneName = fname("spine_03"), AggGeom = { BoxElems = { spineBox } } },
    },
    ConstraintSetup = { { DefaultInstance = joint } },
})
local skelMesh = object({ name = "SK_Body", GetPhysicsAsset = function() return asset end })

local function make_mesh(name, scales)
    local kawaii = { Alpha = 1, bAlphaBoolEnabled = true, RootBone = { BoneName = fname("butt_l") },
        PhysicsSettings = { Radius = 5 }, DummyBoneLength = 0,
        SphericalLimits = { { DrivingBone = { BoneName = fname("butt_l") }, OffsetLocation = v3(1, 2, 3), Radius = 6 } } }
    local dyn = { Alpha = 1, BoundBone = { BoneName = fname("penis_01") }, BoxExtents = v3(2, 2, 10), LocalJointOffset = v3(0, 0, 5),
        SphereCollisionRadius = 3, SphericalLimits = {} }
    local rbn = { Alpha = 1, OverridePhysicsAsset = nil }
    local mainCls = class("AnimBlueprintGeneratedClass /Game/ABP_Main.ABP_Main_C", {
        { "AnimGraphNode_KawaiiPhysics_1", "AnimNode_KawaiiPhysics" },
        { "AnimGraphNode_StateMachine", "AnimNode_StateMachine" },
    }, engineAnim)
    local postCls = class("AnimBlueprintGeneratedClass /Game/PP.PP_C", {
        { "AnimGraphNode_RigidBody", "AnimNode_RigidBody" },
        { "AnimGraphNode_AnimDynamics", "AnimNode_AnimDynamics" },
    }, engineAnim)
    local main = object({ name = "ABP_Main", montage = false,
        AnimGraphNode_KawaiiPhysics_1 = kawaii, AnimGraphNode_StateMachine = {} })
    main.GetClass = function() return mainCls end
    main.IsAnyMontagePlaying = function(self) return self.montage end
    local post = object({ name = "PP", AnimGraphNode_RigidBody = rbn, AnimGraphNode_AnimDynamics = dyn })
    post.GetClass = function() return postCls end
    local m = object({ name = name, scales = scales, main = main, post = post, kawaii = kawaii, dyn = dyn, rbn = rbn,
        bDisablePostProcessBlueprint = false, bDisableRigidBodyAnimNode = false, postRestarts = 0, mainRestarts = 0,
        cloth = false, PhysicsAssetOverride = nil })
    m.GetAnimInstance = function(self) return self.main end
    m.GetPostProcessInstance = function(self) return self.post end
    m.GetSkinnedAsset = function() return skelMesh end
    m.GetBoneName = function() return fname("root") end
    m.GetSocketTransform = function(self, bone, space)
        assert(space == 2)
        local s = self.scales[bone] or self.scales.root or { 1, 1, 1 }
        return { Scale3D = v3(s[1], s[2], s[3]) }
    end
    m.SetDisablePostProcessBlueprint = function(self, off)
        if not off and self.bDisablePostProcessBlueprint then
            self.postRestarts = self.postRestarts + 1
        end
        self.bDisablePostProcessBlueprint = off
    end
    m.SetAllowRigidBodyAnimNode = function(self, allow, reinit)
        if self.bDisableRigidBodyAnimNode == allow then
            self.bDisableRigidBodyAnimNode = not allow
            if reinit then
                self.mainRestarts = self.mainRestarts + 1
            end
        end
    end
    m.IsClothingSimulationSuspended = function(self) return self.cloth end
    m.SuspendClothingSimulation = function(self) self.cloth = true end
    m.ResumeClothingSimulation = function(self) self.cloth = false end
    return m
end

local said = {}
local P = require("uuepbs_physics")
P.setup({ say = function(fmt, ...) said[#said + 1] = string.format(fmt, ...) end, fname = function(n) return n end })

-- capsule maths: a capsule whose Z points along bone X grows with the bone's X scale
do
    local x, y, z = P.rotator_axes({ 90, 0, 0 })
    check(near(math.abs(z[1]), 1) and near(z[3], 0), "pitch 90 turns the capsule axis onto X")
    check(near(P.axis_factor({ 2, 1, 1 }, z), 2) and near(P.axis_factor({ 2, 1, 1 }, y), 1), "axis factors")
end

-- the player's breast 1.5 and butt 1.2, penis long; Height 1.1 on the root
local body = make_mesh("Body", {
    root = { 1.1, 1.1, 1.1 },
    breast_l = { 1.65, 1.65, 1.65 }, -- 1.5 relative to the body
    butt_l = { 1.32, 1.32, 1.32 },   -- 1.2
    penis_01 = { 2.2, 1.1, 1.1 },    -- 2x long
    spine_03 = { 1.1, 1.1, 1.1 },    -- 1: only Height
})
local rigs = { { comp = body, label = "Body", actorKey = "player" } }

-- nothing scaled: nothing touched
P.apply(rigs, {}, 0)
check(breastSphere.Radius == 8 and body.kawaii.PhysicsSettings.Radius == 5 and body.postRestarts == 0, "no scaling, no change")

-- follow
P.apply(rigs, { player = true }, 1000)
check(near(breastSphere.Radius, 12) and near(breastSphere.Center.X, 15), "breast sphere scaled 1.5")
check(near(breastCapsule.Length, 15) and near(breastCapsule.Radius, 6), "breast capsule scaled")
check(spineBox.X == 20, "a bone scaled by Height only keeps its body")
check(near(joint.Pos1.X, 3) and near(joint.ProfileInstance.LinearLimit.Limit, 4.5) and joint.Pos2.X == 5, "joint frame on the scaled side")
check(near(body.kawaii.PhysicsSettings.Radius, 6) and near(body.kawaii.SphericalLimits[1].Radius, 7.2)
    and near(body.kawaii.SphericalLimits[1].OffsetLocation.Z, 3.6), "kawaii radius and limits scaled 1.2")
check(near(body.dyn.BoxExtents.Z, 10 * 1) and near(body.dyn.BoxExtents.X, 4) and near(body.dyn.SphereCollisionRadius, 3 * 2 ^ (1 / 3)),
    "animdynamics box per axis")
check(body.postRestarts == 1 and body.mainRestarts == 1, "post-process and main blueprints restarted once")

-- same scales again: nothing rewritten, no restart
P.apply(rigs, { player = true }, 5000)
check(near(breastSphere.Radius, 12) and body.postRestarts == 1 and body.mainRestarts == 1, "steady: no restart")

-- the slider moves while a montage plays: post restarts, main waits for the montage
body.scales.breast_l = { 2.2, 2.2, 2.2 }
body.main.montage = true
P.apply(rigs, { player = true }, 10000)
check(near(breastSphere.Radius, 16) and body.postRestarts == 2 and body.mainRestarts == 1, "new size, main waits during the montage")
check(P.summary():find("after the animation", 1, true) ~= nil, "summary says it waits")
body.main.montage = false
P.apply(rigs, { player = true }, 11000)
check(body.mainRestarts == 2, "main restarted after the montage")

-- another character with the same asset: the first one keeps deciding its sizes
local npc = make_mesh("Npc", { root = { 1, 1, 1 }, breast_l = { 3, 3, 3 } })
local both = { rigs[1], { comp = npc, label = "Npc", actorKey = "4D5E" } }
P.apply(both, { player = true, ["4D5E"] = true }, 20000)
check(near(breastSphere.Radius, 16), "shared asset keeps the first character's sizes")
local saidShare = false
for _, s in ipairs(said) do
    saidShare = saidShare or s:find("shares its physics asset", 1, true) ~= nil
end
check(saidShare, "sharing is said")
check(near(npc.kawaii.PhysicsSettings.Radius, 5), "npc kawaii untouched (butt 1)")

-- scaling ends: everything back, blueprints restarted
local pr, mr = body.postRestarts, body.mainRestarts
P.apply(both, {}, 30000)
check(breastSphere.Radius == 8 and breastSphere.Center.X == 10 and breastCapsule.Length == 10 and joint.Pos1.X == 2
    and joint.ProfileInstance.LinearLimit.Limit == 3, "asset back")
check(body.kawaii.PhysicsSettings.Radius == 5 and body.dyn.BoxExtents.X == 2 and body.kawaii.SphericalLimits[1].OffsetLocation.Z == 3,
    "node fields back")
check(body.postRestarts == pr + 1 and body.mainRestarts == mr + 1, "restarted to drop the sizes")

-- off: alpha 0, cloth paused; back when the scaling ends
check(P.set_mode("off"), "off accepted")
P.apply(rigs, { player = true }, 40000)
check(body.kawaii.Alpha == 0 and body.kawaii.bAlphaBoolEnabled == false and body.rbn.Alpha == 0 and body.dyn.Alpha == 0, "nodes off")
check(body.cloth == true, "cloth paused")
check(breastSphere.Radius == 8, "off mode does not scale")
P.apply(rigs, {}, 41000)
check(body.kawaii.Alpha == 1 and body.kawaii.bAlphaBoolEnabled == true and body.rbn.Alpha == 1 and body.cloth == false, "nodes and cloth back")

-- cloth the game paused itself stays paused
body.cloth = true
P.apply(rigs, { player = true }, 42000)
P.apply(rigs, {}, 43000)
check(body.cloth == true, "game-paused cloth left alone")
body.cloth = false

-- switching follow -> off while scaled: sizes back, nodes off
P.set_mode("follow")
P.apply(rigs, { player = true }, 50000)
check(near(breastSphere.Radius, 16), "follow again")
P.set_mode("off")
P.apply(rigs, { player = true }, 50100)
check(breastSphere.Radius == 8 and body.rbn.Alpha == 0, "off from follow puts sizes back")

-- keep: nothing touched, the rest put back
P.set_mode("keep")
P.apply(rigs, { player = true }, 60000)
check(body.rbn.Alpha == 1 and breastSphere.Radius == 8 and body.cloth == false, "keep puts everything back")
check(not P.set_mode("bogus"), "unknown mode refused")
check(P.status():find("keep", 1, true) ~= nil, "status names the mode")

-- UE4SS hands out a new Lua object for every engine call (and they are not ==): a steady character must not be
-- re-tracked, and its blueprints not restarted, on every pass. Here every handle is a fresh proxy.
do
    local function handle(obj)
        return setmetatable({}, { __index = obj, __newindex = obj })
    end
    local inner = make_mesh("Fresh", { root = { 1, 1, 1 }, breast_l = { 1.5, 1.5, 1.5 } })
    local gi, gp = inner.GetAnimInstance, inner.GetPostProcessInstance
    inner.GetAnimInstance = function(self) return handle(gi(self)) end
    inner.GetPostProcessInstance = function(self) return handle(gp(self)) end
    P.set_mode("follow")
    local before = #said
    P.apply({ { comp = handle(inner), label = "Fresh", actorKey = "fresh" } }, { fresh = true }, 80000)
    local pr, mr = inner.postRestarts, inner.mainRestarts
    check(pr == 1, "fresh handles: the post-process blueprint restarted once when the sizes came in (" .. pr .. ")")
    for i = 1, 6 do
        P.apply({ { comp = handle(inner), label = "Fresh", actorKey = "fresh" } }, { fresh = true }, 80000 + 700 * i)
    end
    local tracked_again = 0
    for i = before + 1, #said do
        if said[i]:find("physics: Fresh has", 1, true) then
            tracked_again = tracked_again + 1
        end
    end
    check(tracked_again == 1, "fresh handles: the mesh was looked at " .. tracked_again .. " times instead of once")
    check(inner.postRestarts == pr and inner.mainRestarts == mr, "fresh handles: blueprints restarted again on a steady pose ("
        .. inner.postRestarts .. " / " .. inner.mainRestarts .. ")")
    P.apply({}, {}, 90000)
    check(near(breastSphere.Radius, 8), "fresh handles: sizes back")
end

-- a hair mesh with only a post-process blueprint (no main animation instance): its nodes are still found
do
    local hair = make_mesh("Hair", { root = { 1, 1, 1 }, breast_l = { 1.5, 1.5, 1.5 } })
    hair.main = nil
    local before = #said
    P.apply({ { comp = hair, label = "Hair", actorKey = "hair" } }, { hair = true }, 95000)
    local found = false
    for i = before + 1, #said do
        found = found or (said[i]:find("physics: Hair has", 1, true) ~= nil and said[i]:find("AnimNode_RigidBody", 1, true) ~= nil)
    end
    check(found, "post-process nodes found without a main animation instance")
    P.apply({}, {}, 96000)
end

-- restore_all
P.set_mode("follow")
P.apply(rigs, { player = true }, 70000)
P.restore_all()
check(breastSphere.Radius == 8, "restore_all")

if failures > 0 then
    print(failures .. " physics check(s) failed")
    os.exit(1)
end
print("physics: all checks passed")
