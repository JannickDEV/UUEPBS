-- UUEPBS - Looks (Scripts/uuepbs_looks.lua) against a mocked engine.
--   lua looks_test.lua ../../../mod/UUEPBS/Scripts
-- Checks: skin/hair slot classing, parameter discovery through the instance chain, tan / sweat /
-- hair maths, dynamic instances made once and reused, the game's own MID reused, reset writing the
-- original values back and giving the slot its material again, re-dressing, the window report.
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
    return type(a) == "number" and math.abs(a - b) < 1e-4
end

-- ---------------------------------------------------------------- mock engine
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
function FName(s)
    return s
end
local function param_list(names)
    local list = {}
    for _, n in ipairs(names) do
        list[#list + 1] = { ParameterInfo = { Name = { ToString = function() return n end } } }
    end
    return list
end

-- Base material (its own parameters cannot be listed) -> instance (lists overrides).
local function material(name, scalars, vectors, parent)
    return object({ name = name, ScalarParameterValues = param_list(scalars or {}), VectorParameterValues = param_list(vectors or {}),
        Parent = parent, values = {} })
end

local function mid_of(source)
    local mid = object({ name = "MID_" .. source.name, ScalarParameterValues = {}, VectorParameterValues = {}, Parent = source, values = {}, isMid = true })
    local function lookup(m, n)
        while m do
            if m.values[n] ~= nil then
                return m.values[n]
            end
            m = m.Parent
        end
        return nil
    end
    mid.K2_GetScalarParameterValue = function(self, n)
        local v = lookup(self, n)
        return type(v) == "number" and v or 0
    end
    mid.K2_GetVectorParameterValue = function(self, n)
        local v = lookup(self, n)
        return type(v) == "table" and v or { R = 0, G = 0, B = 0, A = 0 }
    end
    mid.SetScalarParameterValue = function(self, n, v) self.values[n] = v end
    mid.SetVectorParameterValue = function(self, n, c) self.values[n] = { R = c.R, G = c.G, B = c.B, A = c.A } end
    return mid
end

local function mesh(slots)
    local comp = object({ slots = slots, mids = 0 })
    comp.GetNumMaterials = function(self) return #self.slots end
    comp.GetMaterialSlotNames = function(self)
        local out = {}
        for _, s in ipairs(self.slots) do
            out[#out + 1] = s.slot
        end
        return out
    end
    comp.GetMaterial = function(self, i) return self.slots[i + 1].mat end
    comp.SetMaterial = function(self, i, m) self.slots[i + 1].mat = m end
    comp.CreateDynamicMaterialInstance = function(self, i, source)
        local cur = self.slots[i + 1].mat
        if cur.isMid then
            return cur -- like the engine: an existing MID in the slot is reused
        end
        self.mids = self.mids + 1
        local mid = mid_of(source)
        self.slots[i + 1].mat = mid
        return mid
    end
    return comp
end

-- ---------------------------------------------------------------- scene
local skinParent = material("M_Skin_Master", {}, {})
skinParent.values = { SkinTint = { R = 0.8, G = 0.6, B = 0.5, A = 1 }, Roughness = 0.5, Wetness = 0.0 }
local skinInst = material("MI_Body_Skin", { "Roughness", "Wetness", "EmissiveStrength" }, { "SkinTint", "EmissiveColor" }, skinParent)
local headInst = material("MI_Head", { "Roughness" }, { "SkinTint" }, skinParent)
local eyeInst = material("MI_Eye", { "Roughness" }, { "Tint" }, skinParent)
local clothInst = material("MI_Shirt", { "Roughness" }, { "Tint" }, skinParent)
local hairParent = material("M_Hair", {}, {})
hairParent.values = { Melanin = 0.6, Redness = 0.2 }
local hairInst = material("MI_Hair_Long", { "Melanin", "Redness" }, {}, hairParent)
-- a slot that already holds the game's own dynamic instance
local gameMid = mid_of(material("MI_Arms", { "Roughness" }, { "SkinTint" }, skinParent))
gameMid.ScalarParameterValues = param_list({ "Roughness" })
gameMid.VectorParameterValues = param_list({ "SkinTint" })

local body = mesh({ { slot = "Body", mat = skinInst }, { slot = "Head", mat = headInst }, { slot = "Eyes", mat = eyeInst }, { slot = "Shirt", mat = clothInst },
    { slot = "Arms", mat = gameMid } })
local hair = mesh({ { slot = "Hair", mat = hairInst } })
local rigs = { { comp = body, address = "B0D1", actorKey = "player" }, { comp = hair, address = "4A12", actorKey = "player" } }

local L = require("uuepbs_looks")
local said = {}
L.setup({ say = function(f, ...) said[#said + 1] = string.format(f, ...) end, chatter = function() end, profile = function() return {} end })

local function wanted(t)
    local w = {}
    for id, v in pairs(t) do
        w["look:" .. id] = { name = "look:" .. id, weight = v }
    end
    w["bellysize"] = { name = "BellySize", weight = 0.5 } -- a real morph rides along and is ignored
    return { player = w }
end

-- ---------------------------------------------------------------- maths
do
    local c = L.skin_tint({ R = 0.8, G = 0.6, B = 0.5, A = 1 }, -2)
    check(c.R < 0.8 and c.G < 0.6 and c.B < 0.5, "tan darkens")
    check(c.R / 0.8 > c.B / 0.5, "tan is warmer (blue darkened most)")
    local p = L.skin_tint({ R = 0.5, G = 0.5, B = 0.5, A = 1 }, 1)
    check(p.R > 0.5 and near(p.R, p.B), "positive tone lightens evenly")
    local h, s, v = L.rgb_to_hsv(L.hsv_to_rgb(0.3, 0.5, 0.7))
    check(near(h, 0.3) and near(s, 0.5) and near(v, 0.7), "hsv round trip")
    local red = L.hair_tint({ R = 0.6, G = 0.3, B = 0.1, A = 1 }, 1, 0, 0) -- half way round: blue-ish
    check(red.B > red.R, "hue 1 turns brown hair blue-ish")
    local grey = L.hair_tint({ R = 0.5, G = 0.5, B = 0.5, A = 1 }, 0.5, 0, 0)
    check(not (near(grey.R, grey.G) and near(grey.G, grey.B)), "hue gives a grey tint a colour")
    check(near(L.scalar_value("Melanin", 0.6, { light = 2 }), 0.1), "lighter hair = less melanin")
    check(near(L.scalar_value("Roughness", 0.5, { sweat = 2 }), 0.2), "sweat lowers roughness")
    check(near(L.scalar_value("Wet", 0.0, { sweat = 2 }), 1.0), "full sweat = fully wet")
    check(L.look_of({ ["look:skin.tone"] = { weight = 0 } }) == nil, "neutral looks are no look")
    check(L.classify("Body|MI_Body_Skin", { Slots = { "*body*" }, ExcludeSlots = {} }, { Slots = { "*hair*" }, ExcludeSlots = {} }) == "skin", "classify skin")
    check(L.classify("Eyes|MI_Eye", { Slots = { "*e*" }, ExcludeSlots = { "*eye*" } }, { Slots = {}, ExcludeSlots = {} }) == nil, "exclude wins")
    -- the built-in patterns: skin parts yes, outfit pieces named after a body part no
    local D = L.DEFAULTS
    for _, c in ipairs({ { "Body|MI_Body_Skin", "skin" }, { "Legs|MI_Legs", "skin" }, { "Arms|MI_Arms", "skin" }, { "Hair|MI_Hair_Long", "hair" },
        { "Legs|MI_Leggings", nil }, { "Arms|MI_ArmSleeve", nil }, { "Body|MI_Body_Shirt", nil }, { "Shorts|MI_Jeans_Shorts", nil },
        { "Chest|MI_Bikini_Top", nil }, { "Torso|MI_Jacket", nil } }) do
        check(L.classify(c[1], D.Skin, D.Hair) == c[2], "default classing of " .. c[1] .. ": " .. tostring(L.classify(c[1], D.Skin, D.Hair)))
    end
    local picked = L.pick({ ["emissivecolor"] = "EmissiveColor", ["skintint"] = "SkinTint" }, { "*tint*", "*col*" })
    check(#picked == 1 and picked[1] == "SkinTint", "first matching pattern wins, glow params never")
    local explicit = L.pick({}, { "BaseSkinColor" })
    check(explicit[1] == "BaseSkinColor", "a name without * is used even when not listed")
end

-- ---------------------------------------------------------------- apply: tan + sweat
check(L.apply(rigs, wanted({ ["skin.tone"] = -2, ["skin.sweat"] = 2 })) == true, "first apply reports a change")
local bodyMid = body.slots[1].mat
check(bodyMid.isMid and bodyMid.Parent == skinInst, "body slot got a dynamic instance of its material")
check(body.slots[2].mat.isMid, "head slot recoloured too (seams match)")
check(body.slots[3].mat == eyeInst, "eyes untouched")
check(body.slots[4].mat == clothInst, "shirt untouched")
check(body.slots[5].mat == gameMid, "the game's own MID is reused, not replaced")
check(body.mids == 2, "two instances made (body, head), got " .. body.mids)
check(bodyMid.values.SkinTint and bodyMid.values.SkinTint.R < 0.8, "skin tint darkened")
check(near(bodyMid.values.Roughness, 0.2), "roughness lowered")
check(near(bodyMid.values.Wetness, 1.0), "wetness raised")
check(bodyMid.values.EmissiveColor == nil and bodyMid.values.EmissiveStrength == nil, "emissive left alone")
check(hair.slots[1].mat == hairInst, "hair untouched while no hair look is set")
local rep = L.report_for("player")
check(rep and rep:find("slots changed", 1, true) and rep:find("roughness", 1, true), "report: " .. tostring(rep))
check(L.report():find("^looks\tplayer\t"), "bridge line")

-- same values again: nothing new is made, values stay
check(L.apply(rigs, wanted({ ["skin.tone"] = -2, ["skin.sweat"] = 2 })) == false, "same apply: no report change")
check(body.mids == 2 and body.slots[1].mat == bodyMid, "instances are reused")
check(not L.watch(), "watch: nothing swapped")

-- sweat off, tone kept: roughness / wetness go back to the game's values
L.apply(rigs, wanted({ ["skin.tone"] = -2 }))
check(near(bodyMid.values.Roughness, 0.5) and near(bodyMid.values.Wetness, 0.0), "sweat reset restores roughness and wetness")
check(bodyMid.values.SkinTint.R < 0.8, "tone stays")

-- hair
L.apply(rigs, wanted({ ["skin.tone"] = -2, ["hair.lightness"] = 2, ["hair.hue"] = 0.5 }))
local hairMid = hair.slots[1].mat
check(hairMid.isMid and near(hairMid.values.Melanin, 0.1), "hair melanin lowered")
check(hairMid.values.Redness and hairMid.values.Redness > 0.2, "hue > 0 adds redness")

-- the game re-dresses the body slot: watch notices, apply starts from the new material
local newSkin = material("MI_Body_Skin_Swim", { "Roughness" }, { "SkinTint" }, skinParent)
body.slots[1].mat = newSkin
check(L.watch(), "watch notices a swapped material")
L.apply(rigs, wanted({ ["skin.tone"] = -2, ["hair.lightness"] = 2, ["hair.hue"] = 0.5 }))
check(body.slots[1].mat.isMid and body.slots[1].mat.Parent == newSkin, "new outfit material recoloured")

-- reset everything: slots get their materials back, the game's MID gets its values back
L.apply(rigs, { player = { bellysize = { name = "BellySize", weight = 0.5 } } })
check(body.slots[1].mat == newSkin, "body material given back")
check(body.slots[2].mat == headInst, "head material given back")
check(hair.slots[1].mat == hairInst, "hair material given back")
check(body.slots[5].mat == gameMid and near(gameMid.values.SkinTint and gameMid.values.SkinTint.R or 0.8, 0.8), "game MID values written back")
check(not L.active() and L.report() == "", "nothing tracked, no report")

-- other characters are separate: an NPC's look does not touch the player
local npcBody = mesh({ { slot = "Body", mat = material("MI_NPC_Skin", {}, { "SkinTint" }, skinParent) } })
rigs[#rigs + 1] = { comp = npcBody, address = "C0FF", actorKey = "C0FF" }
L.apply(rigs, { C0FF = wanted({ ["skin.tone"] = 1 }).player })
check(npcBody.slots[1].mat.isMid and body.slots[1].mat == newSkin, "per character")
check(L.report_for("C0FF") ~= nil and L.report_for("player") == nil, "report per character")

-- profile override: explicit names
L.setup({ say = function() end, chatter = function() end, profile = function()
    return { Looks = { Skin = { Slots = { "Shirt*" }, ExcludeSlots = {}, Tint = { "Tint" } } } }
end })
local shirt = mesh({ { slot = "Shirt", mat = clothInst } })
clothInst.values.Tint = { R = 1, G = 1, B = 1, A = 1 }
L.apply({ { comp = shirt, address = "5A", actorKey = "player" } }, wanted({ ["skin.tone"] = -1 }))
check(shirt.slots[1].mat.isMid and shirt.slots[1].mat.values.Tint.R < 1, "profile Slots/Tint override")

check(#said == 0, "nothing said: " .. table.concat(said, " | "))
if failures == 0 then
    print("looks test OK")
else
    print(failures .. " failure(s)")
    os.exit(1)
end
