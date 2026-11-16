-- UUEPBS - Hand Contact animation names (Scripts/uuepbs_hands.lua) against a mocked engine.
--   lua hands_test.lua ../../../mod/UUEPBS/Scripts
-- Checks: the active montage of each character's main mesh is reported as a "play" line, a
-- single-node animation is used when no montage plays, copies / extra meshes are ignored, a
-- change (and only a change) asks for a bridge rewrite, a broken anim instance does not throw.
local dir = arg[1] or "../../../mod/UUEPBS/Scripts"
package.path = dir .. "/?.lua;" .. package.path

local failures = 0
local function check(cond, what)
    if not cond then
        failures = failures + 1
        print("FAIL: " .. what)
    end
end

local function object(t)
    t.alive = true
    t.IsValid = function(self) return self.alive end
    t.GetFName = function(self)
        return { ToString = function() return self.name end }
    end
    return t
end

local montage = object({ name = "AM_SelfTouch_Chest" })
local inst = object({ name = "ABP_Player_C_0", montage = montage })
inst.GetCurrentActiveMontage = function(self) return self.montage end
local body = object({ name = "CharacterMesh0", inst = inst })
body.GetAnimInstance = function(self) return self.inst end

local idle = object({ name = "A_Idle_Lean" })
local npc = object({ name = "NpcMesh", mode = 1, AnimationData = { AnimToPlay = idle } })
npc.GetAnimInstance = function() return nil end
npc.GetAnimationMode = function(self) return self.mode end

local broken = object({ name = "Broken" })
broken.GetAnimInstance = function() error("boom") end

local hair = object({ name = "Hair", inst = inst })
hair.GetAnimInstance = function(self) return self.inst end

local rigs = {
    { comp = body, primary = true, actorKey = "player" },
    { comp = hair, primary = false, actorKey = "player" },
    { comp = npc, primary = true, actorKey = "0xABC" },
    { comp = broken, primary = true, actorKey = "0xDEF" },
    { comp = body, primary = true, copy = true, actorKey = "0x123" },
}

local H = require("uuepbs_hands")
check(H.poll(rigs) == true, "first poll should report a change")
local r = H.report()
check(r:find("play\t0xABC\tA_Idle_Lean", 1, true) ~= nil, "single-node animation missing: " .. r)
check(r:find("play\tplayer\tAM_SelfTouch_Chest", 1, true) ~= nil, "montage missing: " .. r)
check(r:find("0xDEF", 1, true) == nil, "broken mesh reported")
check(r:find("0x123", 1, true) == nil, "scene copy reported")
check(H.poll(rigs) == false, "nothing changed, no rewrite wanted")

-- montage ends: the line goes, and that is a change
inst.montage = nil
check(H.poll(rigs) == true, "montage ending is a change")
check(H.report():find("player", 1, true) == nil, "ended montage still reported")
-- not single-node mode: its AnimToPlay is ignored
npc.mode = 0
H.poll(rigs)
check(H.report() == "", "anim blueprint mode should not report AnimToPlay: " .. H.report())
check(H.status():find("no montage", 1, true) ~= nil, "status with nothing playing")
-- names with tabs cannot break the bridge file
inst.montage = object({ name = "AM\tWeird\nName" })
H.poll(rigs)
check(H.report() == "play\tplayer\tAM Weird Name", "tabs not cleaned: " .. H.report())

if failures > 0 then
    print(failures .. " failure(s)")
    os.exit(1)
end
print("hands_test: all checks passed")
