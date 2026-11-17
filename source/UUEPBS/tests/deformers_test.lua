-- UUEPBS - mesh deformers (Scripts/uuepbs_deformers.lua) against a mocked engine.
--   lua deformers_test.lua ../../../mod/UUEPBS/Scripts
-- Checks: a scaled character's deformers go off and come back once it no longer scales bones,
-- other characters and meshes without a deformer are left alone, "keep" mode, a released mesh
-- gets its deformer back, a failing SetMeshDeformer is reported once.
local dir = arg[1] or "../../../mod/UUEPBS/Scripts"
package.path = dir .. "/?.lua;" .. package.path

local failures = 0
local function check(cond, what)
    if not cond then
        failures = failures + 1
        print("FAIL: " .. what)
    end
end

local nextAddr = 0x1000
local function object(t)
    nextAddr = nextAddr + 0x10
    local addr = nextAddr
    t.alive = true
    t.IsValid = function(self) return self.alive end
    t.GetAddress = function() return addr end
    t.GetFName = function(self)
        return { ToString = function() return self.name end }
    end
    return t
end

local function mesh(name, deformer)
    local m = object({ name = name, MeshDeformer = deformer, sets = 0 })
    m.SetMeshDeformer = function(self, d)
        if self.broken then
            error("no such function")
        end
        self.sets = self.sets + 1
        self.MeshDeformer = d
    end
    return m
end

local said = {}
local D = require("uuepbs_deformers")
D.setup({ say = function(fmt, ...) said[#said + 1] = string.format(fmt, ...) end })

local dqs = object({ name = "DG_DQS_JiggleColors" })
local body = mesh("CharacterMesh0", dqs)
local eyes = mesh("EyesComponent", nil)
local npc = mesh("NpcBody", dqs)
local rigs = {
    { comp = body, label = "Body", actorKey = "player" },
    { comp = eyes, label = "Eyes", actorKey = "player" },
    { comp = npc, label = "Npc", actorKey = "4D5E" },
}

-- nothing scaled: nothing touched
check(D.apply(rigs, {}, "auto") == 0 and body.sets == 0, "no scaling, no change")

-- the player scales bones: its body's deformer goes off, the NPC keeps its own
check(D.apply(rigs, { player = true }, "auto") == 1, "one deformer switched off")
check(body.MeshDeformer == nil and npc.MeshDeformer == dqs and eyes.sets == 0, "only the scaled character's deformer is off")
check(D.status():find("DG_DQS_JiggleColors on Body", 1, true) ~= nil, "status lists it")
check(D.apply(rigs, { player = true }, "auto") == 0 and body.sets == 1, "repeated apply does nothing")

-- scaling over: the deformer comes back
check(D.apply(rigs, {}, "auto") == 1 and body.MeshDeformer == dqs, "deformer back on")
check(D.status():find("none", 1, true) ~= nil, "status: none off")

-- keep mode never touches them
check(D.apply(rigs, { player = true, ["4D5E"] = true }, "keep") == 0 and body.MeshDeformer == dqs and npc.MeshDeformer == dqs, "keep mode")

-- a mesh that stops being tracked gets its deformer back
D.apply(rigs, { ["4D5E"] = true }, "auto")
check(npc.MeshDeformer == nil, "npc deformer off")
D.apply({ rigs[1], rigs[2] }, { ["4D5E"] = true }, "auto")
check(npc.MeshDeformer == dqs, "released mesh gets its deformer back")

-- the game set its own deformer meanwhile: not overwritten
D.apply(rigs, { player = true }, "auto")
local other = object({ name = "GameSwapped" })
body.MeshDeformer = other
D.apply(rigs, {}, "auto")
check(body.MeshDeformer == other, "a deformer the game set meanwhile is kept")

-- SetMeshDeformer fails: reported once, nothing remembered
local broken = mesh("Broken", dqs)
broken.broken = true
local before = #said
D.apply({ { comp = broken, label = "Broken", actorKey = "x" } }, { x = true }, "auto")
D.apply({ { comp = broken, label = "Broken", actorKey = "x" } }, { x = true }, "auto")
check(#said == before + 1 and said[#said]:find("could not switch", 1, true) ~= nil, "failure said once")
check(broken.MeshDeformer == dqs, "failed mesh untouched")

if failures > 0 then
    print(failures .. " deformer check(s) failed")
    os.exit(1)
end
print("deformers: all checks passed")
