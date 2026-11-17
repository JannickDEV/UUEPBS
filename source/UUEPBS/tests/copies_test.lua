-- Scene copy matching (Scripts/uuepbs_copies.lua) against a mocked engine: which actors count as copies
-- of an edited character.
--   lua copies_test.lua <Scripts dir>
-- Afterparty (UE 5.7) showed two ways NPCs and props were taken for copies of the player:
--   * every NPC wears the player's eye, eyelash and scalp meshes, so "two shared meshes" was met by
--     NPCs with a body of their own (their eyes and hair then got the player's sliders);
--   * props (a laptop, a bicycle) were taken although they share no mesh at all; a match is now
--     checked with GetSkinnedAsset before it is used.
local scripts = arg[1] or "../../../mod/UUEPBS/Scripts"
package.path = scripts .. "/?.lua;" .. package.path
local C = dofile(scripts .. "/uuepbs_copies.lua")

local fails = 0
local function check(cond, msg)
    if not cond then
        fails = fails + 1
        io.stderr:write("FAIL: " .. msg .. "\n")
    end
end

local nextAddr = 0x1000
local function obj(name, fields)
    nextAddr = nextAddr + 0x10
    local o = fields or {}
    o._addr, o._name, o._valid = nextAddr, name, true
    o.IsValid = function(self) return self._valid end
    o.GetAddress = function(self) return self._addr end
    return o
end

-- mesh assets
local body = obj("SK_FemalePenis")
local npcBody = obj("SK_FemaleVagina")
local eyes = obj("SK_FemaleEyes")
local lashes = obj("SK_FemaleEyelashes")
local scalp = obj("SK_Scalp_OOT")
local laptop = obj("SK_Laptop")

local all = {}
-- `told`: what the property read hands back (a wrong read in the laptop case); the engine call always tells the truth
local function comp(name, owner, mesh, told)
    local c = obj(name, { _owner = owner, _mesh = mesh, _told = told or mesh })
    c.GetOwner = function(self) return self._owner end
    all[#all + 1] = c
    return c
end
local function actor(name, controller)
    local a = obj(name, { Controller = controller })
    a.K2_GetComponentsByClass = function(self)
        local list = {}
        for _, c in ipairs(all) do
            if c._valid and c._owner == self then list[#list + 1] = c end
        end
        return list
    end
    return a
end

local player = actor("BP_PlayerCharacter_C_0")
local playerRigs = {
    { comp = comp("CharacterMesh0", player, body), primary = true },
    { comp = comp("EyesComponent", player, eyes) },
    { comp = comp("Eyelash Comp", player, lashes) },
    { comp = comp("Scalp", player, scalp) },
}

local printed = {}
local built = {}
C.setup({
    say = function(fmt, ...) printed[#printed + 1] = string.format(fmt, ...) end,
    chatter = function() end,
    match = "outfit",
    alive = function(o) return o ~= nil and o._valid == true end,
    address_num = function(o) return o and o._addr end,
    mesh_prop = function(c) return c._told and c._told._addr end,
    mesh_asset_addr = function(c) return c._mesh and c._mesh._addr end,
    name_of = function(o) return o._name end,
    full_name = function(o) return o._name end,
    is_default_object = function() return false end,
    still = function(o) return o._valid end,
    each_of = function(list, fn) for _, v in ipairs(list) do fn(v) end end,
    now_ms = function() return 0 end,
    skel_class = function() return {} end,
    controller = function() return nil end,
    find_all = function() return all end,
    build = function(c, src)
        built[#built + 1] = c._name .. "@" .. c._owner._name
        return { comp = c, label = c._name, actorKey = src.key }
    end,
})
for _, r in ipairs(playerRigs) do
    r.actorKey, r.label, r.ownerLabel, r.full, r.meshAddr = "player", r.comp._name, "Player", r.comp._name, r.comp._mesh._addr
end
C.set_sources(playerRigs, { [player._addr] = true })

-- an NPC nobody controls, with a body of its own and the player's eyes, lashes and scalp: not a copy
local npc = actor("NPC_Base_C_8")
comp("CharacterMesh0", npc, npcBody)
comp("Eyes", npc, eyes)
comp("Lashes", npc, lashes)
comp("Scalp", npc, scalp)
-- an AI-controlled NPC the same way: not a copy
local npc2 = actor("NPC_4", obj("AIController_4"))
comp("CharacterMesh0", npc2, npcBody)
comp("Eyes", npc2, eyes)
comp("Lashes", npc2, lashes)
-- a desk with a laptop whose property read hands back the player's body asset: not a copy
local desk = actor("BP_DeskComputer_C_1")
comp("Laptop", desk, laptop, body)
-- a cutscene actor with the player's body, eyes and scalp: a copy (all three meshes)
local cine = actor("BP_Cine_Player_C_1")
comp("Body", cine, body)
comp("Eyes", cine, eyes)
comp("Scalp", cine, scalp)

C.search_now()
C.tick(0)
table.sort(built)
local got = table.concat(built, ", ")
check(got == "Body@BP_Cine_Player_C_1, Eyes@BP_Cine_Player_C_1, Scalp@BP_Cine_Player_C_1", "only the cutscene actor is a copy: " .. got)
check(#C.rigs() == 3, "three copy meshes: " .. #C.rigs())

-- "mesh" match (one shared mesh is enough) still lets a body-less match through, but never a lying property
C.setup({
    say = function() end, chatter = function() end, match = "mesh",
    alive = function(o) return o ~= nil and o._valid == true end,
    address_num = function(o) return o and o._addr end,
    mesh_prop = function(c) return c._told and c._told._addr end,
    mesh_asset_addr = function(c) return c._mesh and c._mesh._addr end,
    name_of = function(o) return o._name end, full_name = function(o) return o._name end,
    is_default_object = function() return false end, still = function(o) return o._valid end,
    each_of = function(list, fn) for _, v in ipairs(list) do fn(v) end end,
    now_ms = function() return 0 end, skel_class = function() return {} end, controller = function() return nil end,
    find_all = function() return all end,
    build = function(c, src)
        built[#built + 1] = c._name .. "@" .. c._owner._name
        return { comp = c, label = c._name, actorKey = src.key }
    end,
})
built = {}
C.set_sources(playerRigs, { [player._addr] = true })
C.search_now()
local laptopTaken = false
for _, b in ipairs(built) do
    laptopTaken = laptopTaken or b:find("Laptop", 1, true) ~= nil
end
check(not laptopTaken, "the laptop is never a copy: " .. table.concat(built, ", "))

-- Carnal Instinct: two NPCs built from the same body and outfit (Damaris and the bard Nenu, both FelianFem
-- with the city skirt and bra). Nenu is a pawn of its own whose body morphs differ: not a copy. A pawn that
-- looks the same (a cutscene stand-in) and a plain menu actor with a different body (no pawn) are copies,
-- and so are morphs our sliders set, expressions or animated in / out morphs that differ.
do
    all = {}
    built = {}
    local felian, skirt, bra, hair = obj("SK_FelianFem_Basemodel_V2"), obj("SK_CitySkirt"), obj("SK_CityBra"), obj("SK_FelHair")
    local function weights(c, w)
        c._morphs = w
        return c
    end
    local damaris = actor("CI_NPC_DLG_Damaris_C_1")
    damaris._pawn = true
    local dRigs = {
        { comp = weights(comp("CharacterMesh0", damaris, felian), { Morph_BreastEnl = 0.6, Morph_HipsWide = 0.3, Morph_LegFat = 0.8, Exp_Happy = 0.7, Morph_PussyIn = 0.0 }), primary = true },
        { comp = comp("Legs", damaris, skirt) },
        { comp = comp("chest", damaris, bra) },
        { comp = comp("hair", damaris, hair) },
    }
    local nenu = actor("CI_NPC_Bard_Nenu_C_1")
    nenu._pawn, nenu._id = true, "Nenu"
    weights(comp("CharacterMesh0", nenu, felian), { Morph_BreastEnl = 0.2, Morph_HipsWide = 0.3, Morph_LegFat = 0.8 })
    comp("skirt", nenu, skirt)
    comp("Top", nenu, bra)
    local standin = actor("BP_Damaris_Cine_C_1")
    standin._pawn = true
    -- same body; the sliders set LegFat on Damaris (0.8 is ours, the game's own is 0), and the face smiles less
    weights(comp("Body", standin, felian), { Morph_BreastEnl = 0.6, Morph_HipsWide = 0.3, Morph_LegFat = 0.0, Exp_Happy = 0.1, Morph_PussyIn = 0.9 })
    comp("Skirt", standin, skirt)
    local capture = actor("BP_PlayerCapture_C_1") -- not a pawn: the old rules
    weights(comp("Body", capture, felian), { Morph_BreastEnl = 0.0 })
    comp("Skirt", capture, skirt)
    local said = {}
    C.setup({
        say = function() end,
        chatter = function(fmt, ...) said[#said + 1] = string.format(fmt, ...) end,
        match = "outfit",
        alive = function(o) return o ~= nil and o._valid == true end,
        address_num = function(o) return o and o._addr end,
        mesh_prop = function(c) return c._told and c._told._addr end,
        mesh_asset_addr = function(c) return c._mesh and c._mesh._addr end,
        name_of = function(o) return o._name end,
        full_name = function(o) return o._name end,
        is_default_object = function() return false end,
        still = function(o) return o._valid end,
        each_of = function(list, fn) for _, v in ipairs(list) do fn(v) end end,
        now_ms = function() return 0 end,
        skel_class = function() return {} end,
        controller = function() return nil end,
        find_all = function() return all end,
        is_pawn = function(a) return a._pawn == true end,
        identity = function(a) return a._id end,
        morph_value = function(c, name) return (c._morphs or {})[name] or 0 end,
        morph_overridden = function(c, name) return c == dRigs[1].comp and name == "Morph_LegFat" end,
        build = function(c, src)
            built[#built + 1] = c._name .. "@" .. c._owner._name
            return { comp = c, label = c._name, actorKey = src.key }
        end,
    })
    for _, r in ipairs(dRigs) do
        r.actorKey, r.label, r.ownerLabel, r.full, r.meshAddr = "D1", r.comp._name, "Damaris", r.comp._name, r.comp._mesh._addr
    end
    dRigs[1].morphs = "Morph_BreastEnl\tMorph_HipsWide\tMorph_LegFat\tExp_Happy\tMorph_PussyIn"
    C.set_sources(dRigs, { [damaris._addr] = true })
    C.search_now()
    C.tick(0)
    table.sort(built)
    local got = table.concat(built, ", ")
    check(got == "Body@BP_Damaris_Cine_C_1, Body@BP_PlayerCapture_C_1, Skirt@BP_Damaris_Cine_C_1, Skirt@BP_PlayerCapture_C_1",
        "an NPC built from the same body and outfit is not a copy; a look-alike pawn and a menu actor are: " .. got)
    local told = false
    for _, line in ipairs(said) do
        told = told or (line:find("CI_NPC_Bard_Nenu_C_1 is not a scene copy", 1, true) ~= nil and line:find("Morph_BreastEnl", 1, true) ~= nil)
    end
    check(told, "the reason is logged: " .. table.concat(said, " | "))
    check(C.report():find("snot\tNenu\t", 1, true) ~= nil, "the window hears that Nenu was turned down: " .. C.report())

    -- Rules: the guesses can be overruled per actor, by identity, engine name or wildcard.
    local function copies_now()
        built = {}
        C.search_now()
        C.tick(0)
        local owners, seen = {}, {}
        for _, r in ipairs(C.rigs()) do
            local o = r.comp._owner._name
            if not seen[o] then
                seen[o] = true
                owners[#owners + 1] = o
            end
        end
        table.sort(owners)
        return table.concat(owners, ", ")
    end
    -- "never": the look-alike stand-in is not a copy any more; the menu actor still is
    C.set_rules({ { mode = "never", who = "BP_Damaris_Cine*" } })
    C.tick(0)
    local got2 = copies_now()
    check(got2 == "BP_PlayerCapture_C_1", "a never rule (wildcard) turns the stand-in down: " .. got2)
    check(C.report():find("srule\tnever\tBP_Damaris_Cine*\t\twindow", 1, true) ~= nil, "the rule is reported: " .. C.report())
    -- "copy": Nenu (by identity) is a copy after all, the morph check is skipped
    C.set_rules({ { mode = "copy", who = "nenu" } })
    C.tick(0)
    local got3 = copies_now()
    check(got3 == "BP_Damaris_Cine_C_1, BP_PlayerCapture_C_1, CI_NPC_Bard_Nenu_C_1", "a copy rule (identity, any case) takes Nenu: " .. got3)
    local ruled = false
    for line in (C.report() .. "\n"):gmatch("([^\n]*)\n") do
        ruled = ruled or (line:sub(1, 11) == "scopy\tNenu\t" and line:sub(-2) == "\t1")
    end
    check(ruled, "the window sees the copy and that a rule made it: " .. C.report())
    -- the window's rule beats config.lua's for the same actor
    C.set_rules({ { mode = "never", who = "Nenu", source = "window" }, { mode = "copy", who = "CI_NPC_*", source = "config" } })
    C.tick(0)
    local got4 = copies_now()
    check(not got4:find("Nenu", 1, true), "the window's never beats config's copy: " .. got4)
    -- "copy <who> <of>": a cutscene actor with a mesh of its own (no shared asset) becomes a copy of Damaris
    local cineBody = obj("SK_Damaris_Cinematic")
    local cine = actor("BP_Seq_Damaris_C_3")
    comp("CineBody", cine, cineBody)
    comp("CineHair", cine, hair)
    check(not copies_now():find("BP_Seq_Damaris", 1, true), "no rule: an actor with its own body is not a copy")
    C.set_rules({ { mode = "copy", who = "BP_Seq_*", of = "damaris" } })
    C.tick(0)
    local got5 = copies_now()
    check(got5:find("BP_Seq_Damaris_C_3", 1, true) ~= nil, "a copy-of rule takes it whole: " .. got5)
    local both = 0
    for _, r in ipairs(C.rigs()) do
        if r.comp._owner == cine then
            both = both + 1
            check(r.actorKey == "D1", "its meshes get Damaris's sliders: " .. tostring(r.actorKey))
        end
    end
    check(both == 2, "both of its meshes (own body and shared hair): " .. both)
    -- the character it copies is no longer sculpted: let go
    C.set_sources({}, {})
    C.tick(0)
    check(#C.rigs() == 0, "copies of a character that is gone are let go: " .. #C.rigs())
    -- a rule naming a character that is not loaded does nothing
    C.set_sources(dRigs, { [damaris._addr] = true })
    C.set_rules({ { mode = "copy", who = "BP_Seq_*", of = "Somebody" } })
    C.tick(0)
    check(not copies_now():find("BP_Seq_Damaris", 1, true), "a copy of a character that is not loaded is not made")
    -- unusable rules are dropped
    C.set_rules({ { mode = "maybe", who = "x" }, { mode = "never", who = "  " } })
    check(#C.rules() == 0, "unusable rules are dropped: " .. #C.rules())
end

if fails > 0 then
    io.stderr:write(fails .. " copies check(s) failed\n")
    os.exit(1)
end
print("copies tests passed")
