-- UUEPBS - readable character and mesh names
--
-- The window lists characters and meshes by a name a player recognises ("Eve") instead of the
-- engine's object name ("BP_Eve_C_2147482611", "00_EVE_01_Body", "SkeletalMeshComponent_2147...").
-- main.lua resolves a name in this order:
--   1. the game's own name properties on the actor or its character data (from_object),
--   2. the game profile's "Names" table (lookup),
--   3. the engine name cleaned up (clean).
-- Only labels change: actor keys, identities and preset files keep using the engine names.
local M = {}

-- Properties games keep a character's shown name in (FText, FString or FName).
M.NAME_PROPS = { "DisplayName", "CharacterName", "CharacterDisplayName", "CharName", "NPCName", "NpcName", "UnitName",
    "FriendlyName", "Nickname", "PlayerName" }
-- Object properties that often point at the character's data asset / row, which holds the name.
M.DATA_PROPS = { "CharacterData", "CharacterInfo", "CharacterDataAsset", "CharacterDefinition", "CharacterProfile",
    "CharacterConfig", "NPCData", "NpcData", "UnitData", "DataAsset" }

local MAX_LEN = 48

-- Asset / class prefixes that are naming conventions, not part of a name.
local PREFIXES = { bp = true, bpc = true, abp = true, sk = true, skm = true, skel = true, sm = true, mesh = true, ch = true, chr = true }
-- All-capital words kept as they are ("NPC Guard"); other all-capital words become "Eve".
local ACRONYMS = { npc = true, ai = true, ui = true, vr = true, ue = true, dlc = true, hq = true }
-- Mesh-part words dropped when a mesh asset names a character ("00_EVE_01_Body" -> "Eve").
local PARTS = { body = true, face = true, head = true, mesh = true, facemesh = true, headmesh = true, bodymesh = true, base = true }
-- Engine component / class names that say nothing about who or what it is.
local GENERIC = { "^skeletalmeshcomponent", "^masterskeletalmeshcomponent", "^charactermesh%d*$", "^mesh%d*$", "^skeletalmesh%d*$",
    "^poseablemeshcomponent", "^default__", "^none$", "^character$", "^pawn$", "^defaultpawn$", "^actor$" }

-- Turns a value read from a property (Lua string, or FText / FString / FName userdata) into a
-- usable name, or nil when it is empty, "None" or not text.
function M.text_of(v)
    local t = type(v)
    if t ~= "string" then
        if t ~= "userdata" and t ~= "table" then
            return nil
        end
        local ok, s = pcall(function()
            return v:ToString()
        end)
        if not ok or type(s) ~= "string" then
            return nil
        end
        v = s
    end
    v = v:gsub("%c", " "):gsub("^%s+", ""):gsub("%s+$", ""):gsub("%s%s+", " ")
    if v == "" or v == "None" or #v > MAX_LEN then
        return nil
    end
    return v
end

local function is_generic(name)
    local l = name:lower()
    for _, p in ipairs(GENERIC) do
        if l:find(p) then
            return true
        end
    end
    return false
end

-- The engine name cleaned up for display, or nil when nothing readable is left:
--   "BP_Rokuv3_C_0" -> "Rokuv3", "00_EVE_01_Body" -> "Eve Body", "SK_Shirt" -> "Shirt",
--   "Gatekeeper_church2_2147465868" -> "Gatekeeper church2", "Appearance.EAppearanceSlot::Legs_C" -> "Legs",
--   "SkeletalMeshComponent_2147482611" -> nil
-- `character`: the name is a mesh asset standing for a whole character, so part words go too
-- ("00_EVE_01_Body" -> "Eve", "Maelle_v2_FaceMesh" -> "Maelle v2").
function M.clean(name, character)
    if type(name) ~= "string" then
        return nil
    end
    -- the last part of a path or of "Outer.Inner" / "Enum::Value"
    local s = name:match("([^/.:%s][^/.:]*)$") or name
    s = s:gsub("^Default__", "")
    if s == "" or is_generic(s) then
        return nil
    end
    local words = {}
    for w in s:gmatch("[^_%-%s]+") do
        words[#words + 1] = w
    end
    -- drop numbers (run-time and placed instance numbers, "00_" sort prefixes, "_01" variants) and UE5 World
    -- Partition actor ids ("_UAID_F4A475A95D2F4F8701_1834711720")
    local kept = {}
    local i = 1
    while i <= #words do
        local w = words[i]
        if w == "UAID" and words[i + 1] and words[i + 1]:match("^%x+$") and #words[i + 1] >= 8 then
            i = i + 1 -- the id after it goes too
        elseif not w:match("^%d+$") then
            kept[#kept + 1] = w
        end
        i = i + 1
    end
    -- naming-convention prefixes in front ("BP_", "SK_"), the class suffix "_C" at the end
    while #kept > 1 and PREFIXES[kept[1]:lower()] do
        table.remove(kept, 1)
    end
    while #kept > 1 and kept[#kept] == "C" do
        table.remove(kept)
    end
    -- a run of short all-capital tags in front of a name ("CI_NPC_DLG_Damaris" -> "Damaris"); a single one
    -- stays ("NPC Merchant", "EVE Body")
    local tags = 0
    while tags < #kept and #kept[tags + 1] <= 3 and kept[tags + 1]:match("^%u+$") do
        tags = tags + 1
    end
    if tags >= 2 and tags < #kept and not kept[tags + 1]:match("^%u+$") then
        for _ = 1, tags do
            table.remove(kept, 1)
        end
    end
    if character then
        for i = #kept, 1, -1 do
            if #kept > 1 and PARTS[kept[i]:lower()] then
                table.remove(kept, i)
            end
        end
    end
    if #kept == 0 or (#kept == 1 and (PREFIXES[kept[1]:lower()] or kept[1] == "C")) then
        return nil
    end
    for i, w in ipairs(kept) do
        if #w >= 3 and w:match("^%u+$") and not ACRONYMS[w:lower()] then
            kept[i] = w:sub(1, 1) .. w:sub(2):lower()
        end
    end
    local out = table.concat(kept, " ")
    out = out:sub(1, 1):upper() .. out:sub(2)
    if is_generic(out:gsub(" ", "")) then
        return nil
    end
    return out:sub(1, MAX_LEN)
end

local function glob(pattern)
    return "^" .. pattern:lower():gsub("[%^%$%(%)%%%.%[%]%+%-%?]", "%%%0"):gsub("%*", ".*") .. "$"
end

-- Prepares a profile "Names" table ({"BP_Eve_C": "Eve", "00_EVE_*": "Eve", "SK_Roku": "Roku"}) for lookup.
-- Returns nil when there is nothing usable in it.
function M.compile(tbl)
    if type(tbl) ~= "table" then
        return nil
    end
    local exact, globs, prefixes, any = {}, {}, {}, false
    for k, v in pairs(tbl) do
        local name = type(k) == "string" and M.text_of(v) or nil
        if name and k ~= "" and k:sub(1, 1) ~= "_" then
            any = true
            if k:find("*", 1, true) then
                globs[#globs + 1] = { pattern = glob(k), len = #k, name = name, key = k }
            else
                exact[k:lower()] = name
                prefixes[#prefixes + 1] = { prefix = k:lower(), name = name, key = k }
            end
        end
    end
    if not any then
        return nil
    end
    -- longer (more specific) entries first; ties by key so the result never depends on table order
    local function order(a, b)
        local la, lb = a.len or #a.prefix, b.len or #b.prefix
        if la ~= lb then
            return la > lb
        end
        return a.key < b.key
    end
    table.sort(globs, order)
    table.sort(prefixes, order)
    return { exact = exact, globs = globs, prefixes = prefixes }
end

-- The display name the compiled "Names" table gives any of `names` (engine names of one character,
-- most specific first), or nil. Exact matches beat wildcards ("Eve_*"), which beat plain prefixes
-- ("BP_Eve" matches "BP_Eve_C_0").
function M.lookup(compiled, names)
    if not compiled then
        return nil
    end
    for _, n in ipairs(names) do
        local hit = type(n) == "string" and compiled.exact[n:lower()]
        if hit then
            return hit
        end
    end
    for _, g in ipairs(compiled.globs) do
        for _, n in ipairs(names) do
            if type(n) == "string" and n:lower():find(g.pattern) then
                return g.name
            end
        end
    end
    for _, p in ipairs(compiled.prefixes) do
        for _, n in ipairs(names) do
            if type(n) == "string" and n:lower():sub(1, #p.prefix) == p.prefix then
                return p.name
            end
        end
    end
    return nil
end

-- The game's own name for `obj`: one of NAME_PROPS on the object itself, else on an object one of
-- DATA_PROPS points at. `read(holder, prop)` returns the property's value or nil (never raises);
-- `alive(obj)` tells a usable object. Returns name, "prop name" (for the log) or nil.
function M.from_object(obj, read, alive)
    for _, p in ipairs(M.NAME_PROPS) do
        local name = M.text_of(read(obj, p))
        if name and not is_generic(name) then
            return name, p
        end
    end
    for _, d in ipairs(M.DATA_PROPS) do
        local data = read(obj, d)
        if data ~= nil and type(data) ~= "string" and alive(data) then
            for _, p in ipairs(M.NAME_PROPS) do
                local name = M.text_of(read(data, p))
                if name and not is_generic(name) then
                    return name, d .. "." .. p
                end
            end
        end
    end
    return nil
end

return M
