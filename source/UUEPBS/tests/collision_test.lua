-- UUEPBS - "Collision follows Height" (Scripts/uuepbs_collision.lua) against a mocked engine.
--   lua collision_test.lua ../../../mod/UUEPBS/Scripts
-- Checks: height line parsing, capsule half height / radius / step height times Height, the feet staying
-- on the floor (actor up, mesh down, BaseTranslationOffset along), crouching and standing up while scaled,
-- shrinking (half height never below the radius), the game's values written back on reset / switch off /
-- actor change, a capsule root without ACharacter properties, no capsule at all, and the camera's
-- Follow height not lifting a camera boom twice when the capsule already raised it.
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

local function scene(name, parent, rel)
    local c = object({ name = name, parent = parent, RelativeLocation = rel or vec(0, 0, 0) })
    c.GetAttachParent = function(self) return self.parent end
    c.K2_SetRelativeLocation = function(self, v)
        self.RelativeLocation = vec(v.X, v.Y, v.Z)
    end
    return c
end

local function capsule(hh, r)
    local c = scene("CollisionCylinder", nil)
    c.CapsuleHalfHeight, c.CapsuleRadius = hh, r
    c.SetCapsuleSize = function(self, nr, nh)
        self.CapsuleRadius, self.CapsuleHalfHeight = nr, math.max(nh, nr)
    end
    return c
end

-- A standard ACharacter standing on the floor at z = 0: capsule root, mesh on it at -half height.
local function character(name, hh, r, step, opts)
    opts = opts or {}
    local cap = capsule(hh, r)
    local a = object({ name = name, z = hh })
    a.K2_GetActorLocation = function(self) return vec(0, 0, self.z) end
    a.K2_SetActorLocation = function(self, v) self.z = v.Z end
    a.K2_GetRootComponent = function() return cap end
    a.wz = function(self) return self.z end
    cap.wz = function() return a.z end
    local mesh = scene("CharacterMesh0", cap, vec(0, 0, -hh + (opts.footOff or 0)))
    if not opts.plain then
        a.CapsuleComponent = cap
        a.Mesh = mesh
        a.BaseTranslationOffset = vec(0, 0, mesh.RelativeLocation.Z)
        a.CharacterMovement = object({ name = "CharMoveComp", MaxStepHeight = step })
    end
    return a, cap, mesh
end
-- where the feet are in the world (mesh origin) and where the capsule's bottom is
local function feet(a, mesh)
    return a.z + mesh.RelativeLocation.Z
end
local function bottom(a, cap)
    return a.z - cap.CapsuleHalfHeight
end

-- the game's own crouch (UE CharacterMovementComponent::Crouch with bCrouchMaintainsBaseLocation)
local function crouch(a, cap, mesh, crouchedHH, defaultR, defaultMeshZ)
    local old = cap.CapsuleHalfHeight
    cap.CapsuleHalfHeight, cap.CapsuleRadius = crouchedHH, defaultR
    a.z = a.z - (old - crouchedHH)
    mesh.RelativeLocation.Z = defaultMeshZ + (old - crouchedHH)
    a.BaseTranslationOffset.Z = mesh.RelativeLocation.Z
end
local function uncrouch(a, cap, mesh, defaultHH, defaultR, defaultMeshZ)
    local old = cap.CapsuleHalfHeight
    cap.CapsuleHalfHeight, cap.CapsuleRadius = defaultHH, defaultR
    a.z = a.z + (defaultHH - old)
    mesh.RelativeLocation.Z = defaultMeshZ
    a.BaseTranslationOffset.Z = defaultMeshZ
end

local world = { player = nil, ["7FF6AB"] = nil, meshes = {} }
local Col = require("uuepbs_collision")
Col.setup({
    actor_of = function(key)
        local a = world[key]
        return a, a and world.meshes[a] or nil
    end,
})
local function run(n)
    for _ = 1, n or 1 do
        Col.tick()
    end
end

-- ---------------------------------------------------------------- parsing
check(not Col.busy(), "idle without height lines")
check(Col.want({}) == false, "no change")
check(Col.want({ player = "x", other = "0" }) == false, "broken heights are dropped")

-- ---------------------------------------------------------------- grow
local hero, cap, mesh = character("BP_Hero_C_0", 90, 34, 45)
world.player = hero
check(Col.want({ player = "2.0000" }) == true, "new wish")
run()
check(near(cap.CapsuleHalfHeight, 180) and near(cap.CapsuleRadius, 68), "capsule x2: " .. cap.CapsuleHalfHeight .. " x " .. cap.CapsuleRadius)
check(near(bottom(hero, cap), 0), "the capsule's bottom stays on the floor: " .. bottom(hero, cap))
check(near(feet(hero, mesh), 0), "the feet stay on the floor: " .. feet(hero, mesh))
check(near(hero.BaseTranslationOffset.Z, mesh.RelativeLocation.Z), "BaseTranslationOffset follows the mesh")
check(near(hero.CharacterMovement.MaxStepHeight, 90), "step height x2")
check(near(Col.raised(hero), 90), "raised by what the half height grew")
run(3)
check(near(hero.z, 180) and near(cap.CapsuleHalfHeight, 180), "steady state: nothing moves on later checks")
check(Col.status():find("BP_Hero_C_0", 1, true) ~= nil, "status names the character: " .. Col.status())

-- ---------------------------------------------------------------- crouch and stand up while scaled
crouch(hero, cap, mesh, 40, 34, -90)
run()
check(near(cap.CapsuleHalfHeight, 80) and near(cap.CapsuleRadius, 68), "crouched capsule x2: " .. cap.CapsuleHalfHeight)
check(near(bottom(hero, cap), 0) and near(feet(hero, mesh), 0), "crouched: feet and capsule on the floor " .. feet(hero, mesh))
check(near(Col.raised(hero), 40), "crouched: raised by the crouched growth")
uncrouch(hero, cap, mesh, 90, 34, -90)
run()
check(near(cap.CapsuleHalfHeight, 180) and near(bottom(hero, cap), 0) and near(feet(hero, mesh), 0), "standing up: x2 again, feet on the floor")

-- ---------------------------------------------------------------- shrink
Col.want({ player = "0.4000" })
run()
-- 90 x 0.4 = 36 < radius 34 x 0.4 = 13.6? no: 36 >= 13.6
check(near(cap.CapsuleHalfHeight, 36) and near(cap.CapsuleRadius, 13.6), "capsule x0.4: " .. cap.CapsuleHalfHeight)
check(near(bottom(hero, cap), 0) and near(feet(hero, mesh), 0), "shrunk: feet on the floor")
check(near(hero.CharacterMovement.MaxStepHeight, 18), "step height x0.4")
check(near(Col.raised(hero), -54), "lowered by what the half height shrank")

-- ---------------------------------------------------------------- reset
Col.want({})
run()
check(near(cap.CapsuleHalfHeight, 90) and near(cap.CapsuleRadius, 34), "reset: the game's capsule")
check(near(hero.z, 90) and near(mesh.RelativeLocation.Z, -90) and near(hero.BaseTranslationOffset.Z, -90), "reset: actor and mesh back")
check(near(hero.CharacterMovement.MaxStepHeight, 45), "reset: the game's step height")
check(not Col.busy() and near(Col.raised(hero), 0), "idle after reset")

-- ---------------------------------------------------------------- the switch
Col.want({ player = "1.5000" })
run()
check(near(cap.CapsuleHalfHeight, 135), "on again x1.5")
Col.set_enabled(false)
check(Col.busy(), "switching off still has a capsule to put back")
run()
check(near(cap.CapsuleHalfHeight, 90) and near(feet(hero, mesh), 0) and near(hero.z, 90), "switched off: the game's capsule")
check(not Col.busy(), "off: idle while a height is wanted")
check(Col.status():find("off", 1, true) ~= nil, "status says off: " .. Col.status())
Col.set_enabled(true)
run()
check(near(cap.CapsuleHalfHeight, 135), "switched on: sized again")

-- ---------------------------------------------------------------- the actor behind a key changes
local hero2, cap2, mesh2 = character("BP_Hero_C_1", 88, 30, 40)
world.player = hero2
run()
check(near(cap.CapsuleHalfHeight, 90) and near(hero.z, 90), "the old character gets its capsule back")
check(near(cap2.CapsuleHalfHeight, 132) and near(feet(hero2, mesh2), 0), "the new one is sized")
-- briefly missing from the mesh list: kept as it is
world.player = nil
run()
check(near(cap2.CapsuleHalfHeight, 132), "a character briefly not found keeps its capsule")
world.player = hero2
Col.want({})
run()
check(near(cap2.CapsuleHalfHeight, 88), "second character reset")

-- ---------------------------------------------------------------- a mesh sunk a little into the floor
local sunk, capS, meshS = character("BP_Sunk_C_0", 90, 34, 45, { footOff = -2 })
world.player = sunk
Col.want({ player = "1.2000" })
run()
check(near(feet(sunk, meshS), -2), "the mesh keeps its own offset from the capsule's bottom: " .. feet(sunk, meshS))
Col.want({})
run()
check(near(meshS.RelativeLocation.Z, -92), "sunk mesh restored")

-- ---------------------------------------------------------------- capsule root without ACharacter properties
local plain, capP, meshP = character("BP_Pawn_C_0", 60, 20, nil, { plain = true })
world["7FF6AB"], world.meshes[plain] = plain, meshP
Col.want({ ["7FF6AB"] = "2.0000" })
run()
check(near(capP.CapsuleHalfHeight, 120) and near(feet(plain, meshP), 0), "capsule root + the rig's mesh: sized, feet on the floor")
Col.want({})
run()
check(near(capP.CapsuleHalfHeight, 60) and near(meshP.RelativeLocation.Z, -60), "plain pawn restored")

-- ---------------------------------------------------------------- no capsule at all
local thing = object({ name = "BP_Prop_C_0" })
thing.K2_GetRootComponent = function() return scene("DefaultSceneRoot") end
world.player = thing
Col.want({ player = "2.0000" })
run()
check(Col.status():find("no capsule", 1, true) ~= nil, "no capsule is reported: " .. Col.status())
Col.want({})
run()

-- ---------------------------------------------------------------- with the camera's Follow height
-- camera boom on the capsule, 60 cm above its centre; feet at 0 -> pivot 150 cm above the feet.
local Cam = require("uuepbs_camera")
local CLASS_ARM, CLASS_CAM = { "arm" }, { "cam" }
local classes = { ["/Script/Engine.SpringArmComponent"] = CLASS_ARM, ["/Script/Engine.CameraComponent"] = CLASS_CAM }
local pc, pcap, pmesh = character("BP_Player_C_0", 90, 34, 45)
local boom = scene("CameraBoom", pcap, vec(0, 0, 60))
boom.cls, boom.TargetArmLength, boom.TargetOffset = CLASS_ARM, 300, vec(0, 0, 0)
boom.K2_GetComponentLocation = function(self) return vec(0, 0, pc.z + self.RelativeLocation.Z) end
local follow = scene("FollowCamera", boom)
follow.cls = CLASS_CAM
follow.IsActive = function() return true end
pc.comps = { boom, follow }
pc.K2_GetComponentsByClass = function(self, cls)
    local out = {}
    for _, c in ipairs(self.comps) do
        if c.cls == cls then
            out[#out + 1] = c
        end
    end
    return out
end
world.player = pc
Cam.setup({
    engine_class = function(path) return classes[path] end,
    pawn = function() return pc end,
    controller = function() return nil end,
    key_of = function() return "player" end,
    feet_z = function() return feet(pc, pmesh) end,
    raised = function(p) return Col.raised(p) end,
})
local function camera_top()
    return pc.z + boom.RelativeLocation.Z + boom.TargetOffset.Z
end
check(near(camera_top(), 150), "camera pivot starts 150 cm above the feet")
Col.want({ player = "2.0000" })
Cam.want({ player = "0.00\t1.0000\t1\t2.0000" })
run()
Cam.tick()
check(near(camera_top(), 300), "collision first, then follow: the pivot doubles once, not twice: " .. camera_top())
Cam.tick()
Col.tick()
check(near(camera_top(), 300), "steady: " .. camera_top())
-- the other order: the camera is captured before the capsule grows
Cam.want({})
Col.want({})
Cam.tick()
run()
check(near(camera_top(), 150) and near(pc.z, 90), "both back to the game's values: " .. camera_top())
Cam.want({ player = "0.00\t1.0000\t1\t2.0000" })
Cam.tick()
check(near(camera_top(), 300), "follow alone doubles the pivot: " .. camera_top())
Col.want({ player = "2.0000" })
run()
Cam.tick()
check(near(camera_top(), 300), "then the capsule grows: the camera stays framed: " .. camera_top())
-- follow off: the boom simply rides on the capsule
Cam.want({})
Cam.tick()
check(near(camera_top(), 240), "no follow: the boom moves with the capsule only: " .. camera_top())
Col.reset()
check(near(pc.z, 90) and near(pcap.CapsuleHalfHeight, 90), "reset() puts every capsule back")

if failures > 0 then
    print(failures .. " collision check(s) failed")
    os.exit(1)
end
print("collision_test: all checks passed")
