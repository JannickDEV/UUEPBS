-- UUEPBS - Character Scale > Camera (Scripts/uuepbs_camera.lua) against a mocked engine.
--   lua camera_test.lua ../../../mod/UUEPBS/Scripts
-- Checks: cam line parsing, the spring arm the active camera hangs on, Follow height (pivot above the
-- feet), manual lift and distance, the game changing the values (zoom) vs easing back, the game's values
-- written back on reset / pawn change, the camera component and eye height fallbacks, a camera on the mesh.
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
    return type(a) == "number" and math.abs(a - b) < 1e-3
end

-- ---------------------------------------------------------------- mock engine
local nextAddr = 0x1000
local function object(t)
    nextAddr = nextAddr + 0x10
    local a = nextAddr
    t.alive = true
    t.IsValid = function(self) return self.alive end
    t.GetAddress = function() return a end
    t.GetFName = function(self)
        return { ToString = function() return self.name end }
    end
    return t
end
local function vec(x, y, z)
    return { X = x, Y = y, Z = z }
end

local CLASS_ARM, CLASS_CAM = { "arm" }, { "cam" }
local classes = { ["/Script/Engine.SpringArmComponent"] = CLASS_ARM, ["/Script/Engine.CameraComponent"] = CLASS_CAM }

local function actor(name, z, comps)
    local a = object({ name = name, comps = comps or {}, z = z })
    a.K2_GetComponentsByClass = function(self, cls)
        local out = {}
        for _, c in ipairs(self.comps) do
            if c.cls == cls then
                out[#out + 1] = c
            end
        end
        return out
    end
    a.K2_GetActorLocation = function(self) return vec(0, 0, self.z) end
    return a
end
local function arm(name, z, len, offsetZ)
    local c = object({ name = name, cls = CLASS_ARM, TargetArmLength = len, TargetOffset = vec(0, 0, offsetZ or 0), wz = z })
    c.K2_GetComponentLocation = function(self) return vec(0, 0, self.wz) end
    return c
end
local function camera(name, parent, rel, active)
    local c = object({ name = name, cls = CLASS_CAM, parent = parent, RelativeLocation = rel or vec(0, 0, 0), active = active ~= false })
    c.GetAttachParent = function(self) return self.parent end
    c.IsActive = function(self) return self.active end
    c.K2_GetComponentLocation = function(self)
        local pz = self.parent and self.parent.wz or 0
        return vec(0, 0, pz + self.RelativeLocation.Z)
    end
    c.K2_SetRelativeLocation = function(self, v)
        self.RelativeLocation = vec(v.X, v.Y, v.Z)
    end
    return c
end
local function mesh_comp(z)
    local m = object({ name = "CharacterMesh0", wz = z })
    m.K2_GetComponentLocation = function(self) return vec(0, 0, self.wz) end
    m.GetNumBones = function() return 120 end
    return m
end

local state = { pawn = nil, key = "player", feet = 0 }
local Cam = require("uuepbs_camera")
Cam.setup({
    engine_class = function(path) return classes[path] end,
    pawn = function() return state.pawn end,
    controller = function() return nil end,
    key_of = function() return state.key end,
    feet_z = function() return state.feet end,
})
local function want(lines)
    return Cam.want(lines)
end
local function run(n)
    for _ = 1, n or 1 do
        Cam.tick()
    end
end

-- ---------------------------------------------------------------- parsing
check(Cam.parse("-12.50\t1.2500\t1\t1.2000").follow == true, "parse follow")
check(near(Cam.parse("-12.50\t1.2500\t1\t1.2000").lift, -12.5), "parse lift")
check(Cam.parse("x\t1\t0\t1") == nil and Cam.parse("1\t0\t0\t1") == nil and Cam.parse("") == nil, "broken lines are dropped")
check(not Cam.busy(), "idle without cam lines")
check(want({}) == false, "no change")

-- ---------------------------------------------------------------- spring arm, follow height
-- capsule at z=90, feet (mesh) at 0, arm at z=150 (60 above the capsule centre), arm length 300.
local boom = arm("CameraBoom", 150, 300, 10)
local other = arm("AimArm", 150, 120)
local follow = camera("FollowCamera", boom, vec(0, 0, 0), true)
local aimCam = camera("AimCamera", other, vec(0, 0, 0), false)
local pawn = actor("BP_Hero_C_0", 90, { other, aimCam, boom, follow })
state.pawn = pawn
check(want({ player = "0.00\t1.0000\t1\t1.2000" }) == true, "new wish")
run()
-- pivot = 150 + 10 - 0 = 160 cm above the feet; Height 1.2 -> +32 cm, arm x1.2
check(near(boom.TargetOffset.Z, 10 + 32), "follow lifts the active camera's arm: " .. tostring(boom.TargetOffset.Z))
check(near(boom.TargetArmLength, 360), "follow scales the arm length")
check(near(other.TargetArmLength, 120) and near(other.TargetOffset.Z, 0), "the inactive camera's arm is left alone")
check(Cam.status():find("CameraBoom", 1, true) ~= nil, "status names the boom: " .. Cam.status())

-- manual lift and distance on top
want({ player = "-20.00\t1.5000\t1\t1.2000" })
run()
check(near(boom.TargetOffset.Z, 10 + 32 - 20), "manual lift adds to follow")
check(near(boom.TargetArmLength, 300 * 1.5 * 1.2), "distance multiplies")

-- the game zooms out (new value outside the range): the offset goes on top of the new value
boom.TargetArmLength = 600
run()
check(near(boom.TargetArmLength, 600 * 1.8), "zoom is the new base: " .. boom.TargetArmLength)
-- the game eases back from ours towards its own: no runaway
boom.TargetArmLength = 800 -- between 600 and 1080
run(5)
check(near(boom.TargetArmLength, 1080), "easing back keeps the game's value: " .. boom.TargetArmLength)
run(3)
check(near(boom.TargetArmLength, 1080) and near(boom.TargetOffset.Z, 22), "steady state")

-- reset: the game's values come back
want({})
run()
check(near(boom.TargetOffset.Z, 10) and near(boom.TargetArmLength, 600), "reset restores the game's values")
check(not Cam.busy(), "idle after restore")

-- follow alone at Height 1 is never sent by the DLL; lift only:
want({ player = "25.00\t1.0000\t0\t1.3000" })
run()
check(near(boom.TargetOffset.Z, 35) and near(boom.TargetArmLength, 600), "no follow: Height ignored")

-- another character becomes the pawn: the old one gets its values back
local pawn2 = actor("BP_Other_C_1", 90, { arm("Boom2", 150, 250) })
state.pawn, state.key = pawn2, "7FF6AB"
run()
check(near(boom.TargetOffset.Z, 10), "old pawn restored on switch")
check(near(pawn2.comps[1].TargetOffset.Z, 0), "new pawn without a wish is untouched")
want({ ["7FF6AB"] = "-10.00\t1.0000\t0\t1.0000" })
run()
check(near(pawn2.comps[1].TargetOffset.Z, -10), "wish follows the actor key")
want({})
run()

-- ---------------------------------------------------------------- camera component fallback
local cam3 = camera("Camera", nil, vec(-200, 0, 70), true)
local pawn3 = actor("BP_NoBoom_C_0", 90, { cam3 })
state.pawn, state.key, state.feet = pawn3, "player", 0
-- the parentless mock camera sits at world z 70 -> pivot 70 cm
want({ player = "5.00\t1.5000\t1\t2.0000" })
run()
check(near(cam3.RelativeLocation.Z, 70 + 70 + 5), "camera: follow + lift on the relative Z " .. cam3.RelativeLocation.Z)
check(near(cam3.RelativeLocation.X, -200 * 3), "camera: distance scales the relative X")
want({})
run()
check(near(cam3.RelativeLocation.Z, 70) and near(cam3.RelativeLocation.X, -200), "camera restored")

-- first person: a camera on the mesh only gets the manual offset
local m = mesh_comp(0)
local fp = camera("FirstPersonCamera", m, vec(0, 10, 160), true)
local pawn4 = actor("BP_FP_C_0", 90, { fp })
state.pawn = pawn4
want({ player = "8.00\t1.0000\t1\t1.5000" })
run()
check(near(fp.RelativeLocation.Z, 168), "camera on the mesh: no follow lift " .. fp.RelativeLocation.Z)
want({})
run()

-- ---------------------------------------------------------------- eye height fallback
local pawn5 = actor("BP_Plain_C_0", 90, {})
pawn5.BaseEyeHeight = 64
state.pawn, state.feet = pawn5, 0
-- pivot = 90 + 64 - 0 = 154
want({ player = "0.00\t2.0000\t1\t1.1000" })
run()
check(near(pawn5.BaseEyeHeight, 64 + 15.4), "eye height follows " .. pawn5.BaseEyeHeight)
check(Cam.status():find("not adjustable", 1, true) ~= nil, "eye height has no distance: " .. Cam.status())
want({})
run()
check(near(pawn5.BaseEyeHeight, 64), "eye height restored")

-- nothing at all
local pawn6 = actor("BP_Nothing_C_0", 90, {})
state.pawn = pawn6
want({ player = "10.00\t1.0000\t0\t1.0000" })
run()
check(Cam.status():find("no spring arm", 1, true) ~= nil, "nothing found is reported")
-- a dead pawn does not break the tick
pawn6.alive = false
state.pawn = nil
run()
check(Cam.status():find("no controlled", 1, true) ~= nil, "no pawn")

if failures == 0 then
    print("camera tests passed")
    os.exit(0)
end
print(failures .. " failure(s)")
os.exit(1)
