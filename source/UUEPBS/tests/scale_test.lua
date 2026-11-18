-- UUEPBS - Height done by the engine (Scripts/uuepbs_scale.lua) against a mocked engine.
--   lua scale_test.lua ../../../mod/UUEPBS/Scripts
-- Checks: nothing happens while Height is still moving; "escale" lines go out for the top mesh and the meshes
-- hanging on it before anything is scaled; a mesh is scaled only once the DLL acknowledged the factor for it
-- and for every mesh on it; meshes on another mesh are never scaled twice; a mesh with absolute scale is
-- scaled on its own; the game's scale comes back (after the DLL let go of the factor) when Height goes back
-- to 1, when the mesh is let go and when switched off; a scale the game sets itself is kept as its own;
-- a DLL that never answers (older build) means no mesh is ever scaled.
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

local nextAddr = 0x1000
local function comp(name, parent, absolute)
    nextAddr = nextAddr + 0x10
    local a = nextAddr
    local c = { name = name, parent = parent, alive = true, RelativeScale3D = { X = 1, Y = 1, Z = 1 }, writes = 0 }
    c.IsValid = function(self) return self.alive end
    c.GetAddress = function() return a end
    c.GetAttachParent = function(self) return self.parent end
    c.IsUsingAbsoluteScale = function() return absolute == true end
    c.SetRelativeScale3D = function(self, v)
        self.RelativeScale3D = { X = v.X, Y = v.Y, Z = v.Z }
        self.writes = self.writes + 1
    end
    c.hex = string.format("%X", a)
    return c
end

local mod = dofile(dir .. "/uuepbs_scale.lua")
mod.setup({})

local capsule = comp("CollisionCylinder", nil)
local body = comp("CharacterMesh0", capsule)
local head = comp("Head", body)
local hair = comp("Hair", head)
local cape = comp("Cape", body, true) -- absolute scale: not inherited
local rigs = {
    { comp = body, actorKey = "player", label = "body" },
    { comp = head, actorKey = "player", label = "head" },
    { comp = hair, actorKey = "player", label = "hair" },
    { comp = cape, actorKey = "player", label = "cape" },
}

local function lines()
    local t = {}
    for a, f in mod.report():gmatch("escale\t(%x+)\t([%d%.]+)") do
        t[a] = tonumber(f)
    end
    return t
end
local function count(t)
    local n = 0
    for _ in pairs(t) do
        n = n + 1
    end
    return n
end
local function ack(t)
    local out = {}
    for a, f in pairs(t) do
        out[a] = string.format("%.4f", f)
    end
    return out
end

local t = 0
local function step(ms)
    t = t + (ms or 100)
    return mod.tick(rigs, t)
end

-- nothing to do
mod.want({}, {})
check(not step() and mod.report() == "" and not mod.busy(), "idle")

-- Height moving: left to the DLL
mod.want({ player = "1.3000" }, {})
step(100)
mod.want({ player = "1.5000" }, {})
step(100)
step(300)
check(mod.report() == "", "no engine scale while Height still moves")
check(near(body.RelativeScale3D.Z, 1), "body untouched while Height moves")

-- Height still for long enough: the factor is offered, nothing scaled yet
local changed = step(400)
local l = lines()
check(changed, "escale lines changed")
check(near(l[body.hex], 1.5) and near(l[head.hex], 1.5) and near(l[hair.hex], 1.5) and near(l[cape.hex], 1.5) and count(l) == 4,
    "every mesh reported at 1.5: " .. mod.report())
check(near(body.RelativeScale3D.Z, 1) and near(cape.RelativeScale3D.Z, 1), "nothing scaled before the DLL answers")

-- the DLL took it off the body only: still waiting for the meshes hanging on it
mod.want({ player = "1.5000" }, ack({ [body.hex] = 1.5 }))
step()
check(near(body.RelativeScale3D.Z, 1), "body waits for its head and hair")
check(near(cape.RelativeScale3D.Z, 1), "cape waits for its own acknowledgement")

-- everything acknowledged: body and cape scaled, head and hair inherit
mod.want({ player = "1.5000" }, ack(l))
check(not step(), "no new lines once scaled")
check(near(body.RelativeScale3D.X, 1.5) and near(body.RelativeScale3D.Y, 1.5) and near(body.RelativeScale3D.Z, 1.5), "body scaled 1.5")
check(near(cape.RelativeScale3D.Z, 1.5), "absolute-scale cape scaled on its own")
check(near(head.RelativeScale3D.Z, 1) and near(hair.RelativeScale3D.Z, 1), "head and hair inherit, not scaled twice")
check(mod.summary():find("x1.500", 1, true) ~= nil, "summary lists the scaled meshes: " .. mod.summary())
local writes = body.writes
step()
step()
check(body.writes == writes, "no rewrites while nothing changes")

-- the game sets its own scale (e.g. a cutscene resets it): taken as the game's, ours goes on top
body.RelativeScale3D = { X = 1, Y = 1, Z = 1 }
step()
step()
check(near(body.RelativeScale3D.Z, 1.5), "ours again on top of the game's reset: " .. body.RelativeScale3D.Z)

-- Height back to 1: the DLL must let go first
mod.want({}, ack(l))
check(step(), "lines change when Height goes back")
l = lines()
check(near(l[body.hex], 1) and near(l[cape.hex], 1), "factor 1 offered for the scaled meshes: " .. mod.report())
check(near(body.RelativeScale3D.Z, 1.5), "still scaled until the DLL lets go")
mod.want({}, {})
step()
check(near(body.RelativeScale3D.Z, 1) and near(cape.RelativeScale3D.Z, 1), "game's scale back")
step()
check(mod.report() == "" and not mod.busy(), "nothing left: " .. mod.report())

-- a game scale other than 1 is kept and multiplied
body.RelativeScale3D = { X = 0.9, Y = 0.9, Z = 0.9 }
mod.want({ player = "2.0000" }, {})
step(100)
step(700)
mod.want({ player = "2.0000" }, ack(lines()))
step()
check(near(body.RelativeScale3D.Z, 1.8), "game's 0.9 times Height 2: " .. body.RelativeScale3D.Z)

-- the mesh is let go (character out of range): the game's scale back at once
local saved = rigs
rigs = { { comp = cape, actorKey = "player", label = "cape" } }
step()
check(near(body.RelativeScale3D.Z, 0.9), "let-go mesh back to the game's scale: " .. body.RelativeScale3D.Z)
rigs = saved

-- switched off: everything back, nothing offered
mod.set_enabled(false)
mod.want({ player = "2.0000" }, ack(lines()))
step()
l = lines()
check(near(l[cape.hex], 1) or l[cape.hex] == nil, "off: only factor 1 offered")
mod.want({ player = "2.0000" }, {})
step()
step()
check(near(cape.RelativeScale3D.Z, 1) and near(body.RelativeScale3D.Z, 0.9), "off: game's scales back")
check(mod.report() == "", "off: no lines")
mod.set_enabled(true)

-- an older DLL never answers: nothing is ever scaled
local old = dofile(dir .. "/uuepbs_scale.lua")
old.setup({})
local solo = comp("CharacterMesh0", nil)
local soloRigs = { { comp = solo, actorKey = "4D5E" } }
old.want({ ["4D5E"] = "1.4000" }, {})
for i = 1, 20 do
    old.tick(soloRigs, i * 200)
end
check(old.report():find(solo.hex, 1, true) ~= nil, "factor offered")
check(near(solo.RelativeScale3D.Z, 1), "never scaled without an answer")

if failures == 0 then
    print("scale tests passed")
else
    print(failures .. " failure(s)")
    os.exit(1)
end
