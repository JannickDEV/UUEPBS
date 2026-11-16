-- Host test for Scripts/uuepbs_portraits.lua with a mocked engine (no game needed).
--   lua portraits_test.lua ../../../mod/UUEPBS/Scripts /tmp/uuepbs_portraits
-- Checks: request file parsing, one picture per gap, the head-shot call sequence (spawn a
-- SceneCapture2D, show only the character, capture, export, destroy), the game-portrait path,
-- the read-back PNG fallback, and that a failing character is given up after two tries.
-- The mocked UFunctions check their argument count the way UE4SS does ("UFunction expected 7
-- parameters, received 6"), against UE4.26 signatures by default and UE5.3 ones in section 10.
local scripts, dir = arg[1], arg[2] or "/tmp/uuepbs_portraits"
package.path = scripts .. "/?.lua;" .. package.path
os.execute('mkdir -p "' .. dir .. '/_portraits"')

local failures = 0
local function check(cond, msg)
    if not cond then
        failures = failures + 1
        print("FAIL: " .. msg)
    end
end

local calls = {}
local function obj(t)
    t = t or {}
    t.IsValid = function()
        return true
    end
    return t
end

-- Mocked engine -------------------------------------------------------------------------
-- Parameter counts (return value not counted), per engine version.
local UE4 = {
    CreateRenderTarget2D = 6, ReleaseRenderTarget2D = 1, ExportRenderTarget = 4, ReadRenderTargetRaw = 3,
    BeginDrawCanvasToRenderTarget = 5, EndDrawCanvasToRenderTarget = 2, K2_DrawTexture = 9,
    BeginDeferredActorSpawnFromClass = 5, FinishSpawningActor = 2, ShowOnlyActorComponents = 2, GetActorBounds = 4,
    CreateCanvasRenderTarget2D = 4, UpdateResourceImmediate = 1,
    SetMobility = 1, SetAttenuationRadius = 1, SetIntensity = 1,
}
local UE53 = {}
for k, v in pairs(UE4) do
    UE53[k] = v
end
UE53.CreateRenderTarget2D = 7 -- bSupportUAVs
UE53.BeginDeferredActorSpawnFromClass = 6 -- TransformScaleMethod
UE53.FinishSpawningActor = 3 -- TransformScaleMethod
local sig = UE4
local argCounts = {}
local function ufn(name, fn)
    return function(self, ...)
        local n = select("#", ...)
        if sig[name] and n ~= sig[name] then
            error(string.format("[UFunction::setup_metamethods -> __call] UFunction expected %d parameters, received %d", sig[name], n))
        end
        argCounts[name] = n
        return fn(self, ...)
    end
end
local kismetRT = true
local exportWorks = true
local spawnWorks = true
local captures = {}
local lights = {} -- every studio light spawned: { comp, xf, destroyed }
local suns = { { Intensity = 10, bAffectsWorld = true, bVisible = true } }
function FindAllOf(name)
    if name == "DirectionalLightComponent" then
        return suns
    end
    return nil
end
local pointLightClass = obj({ name = "PointLight" })
local rendering = obj({
    CreateRenderTarget2D = ufn("CreateRenderTarget2D", function(_, world, w, h, fmt)
        if not kismetRT then
            error("blocked in this game", 0)
        end
        calls[#calls + 1] = "rt " .. w .. "x" .. h .. " fmt " .. fmt
        return obj({ kind = "rt" })
    end),
    ReleaseRenderTarget2D = ufn("ReleaseRenderTarget2D", function()
        calls[#calls + 1] = "release"
    end),
    ExportRenderTarget = ufn("ExportRenderTarget", function(_, world, rt, folder, file)
        calls[#calls + 1] = "export " .. file
        if exportWorks then
            local f = assert(io.open(folder .. "\\" .. file, "wb"))
            f:write(string.rep("x", 100))
            f:close()
        end
    end),
    ReadRenderTargetRaw = ufn("ReadRenderTargetRaw", function()
        calls[#calls + 1] = "readraw"
        return {
            ForEach = function(_, fn)
                for i = 1, 256 * 256 do
                    fn(i, { get = function() return { R = 0.5, G = 0.25, B = 1, A = 1 } end })
                end
            end,
        }
    end),
    BeginDrawCanvasToRenderTarget = ufn("BeginDrawCanvasToRenderTarget", function(_, world, rt, canvasOut, sizeOut, ctxOut)
        calls[#calls + 1] = "canvas"
        return obj({ K2_DrawTexture = ufn("K2_DrawTexture", function(_, tex) calls[#calls + 1] = "draw " .. tex.name end) }), { X = 256, Y = 256 }, { ctx = true }
    end),
    EndDrawCanvasToRenderTarget = ufn("EndDrawCanvasToRenderTarget", function()
        calls[#calls + 1] = "endcanvas"
    end),
})
local canvasRT = obj({
    CreateCanvasRenderTarget2D = ufn("CreateCanvasRenderTarget2D", function(_, world, cls, w, h)
        calls[#calls + 1] = "canvasrt " .. w .. "x" .. h
        return obj({
            kind = "canvasrt",
            RenderTargetFormat = 13,
            UpdateResourceImmediate = ufn("UpdateResourceImmediate", function(self)
                calls[#calls + 1] = "update fmt " .. self.RenderTargetFormat
            end),
        })
    end),
})
local statics = obj({
    BeginDeferredActorSpawnFromClass = ufn("BeginDeferredActorSpawnFromClass", function(_, world, cls, xf)
        if cls == pointLightClass then
            calls[#calls + 1] = "light"
            local rec = { xf = xf, setBefore = {} }
            local comp
            comp = obj({
                Intensity = 5000, IntensityUnits = 0, Mobility = 1, AttenuationRadius = 1000, CastShadows = true,
                SetMobility = ufn("SetMobility", function(self, m) self.Mobility = m end),
                SetAttenuationRadius = ufn("SetAttenuationRadius", function(self, r) self.AttenuationRadius = r end),
                SetIntensity = ufn("SetIntensity", function(self, v)
                    if self.Mobility ~= 2 then
                        error("not movable", 0)
                    end
                    self.Intensity = v
                    rec.pushed = v
                end),
            })
            rec.comp = comp
            lights[#lights + 1] = rec
            return obj({ PointLightComponent = comp, K2_DestroyActor = function()
                calls[#calls + 1] = "unlight"
                rec.destroyed = true
            end })
        end
        calls[#calls + 1] = "spawn"
        if not spawnWorks then
            return nil
        end
        local comp = obj({
            ShowOnlyActorComponents = ufn("ShowOnlyActorComponents", function(_, actor) calls[#calls + 1] = "showonly " .. actor.name end),
            CaptureScene = function(self)
                calls[#calls + 1] = "capture"
                captures[#captures + 1] = self
            end,
        })
        return obj({ CaptureComponent2D = comp, K2_DestroyActor = function() calls[#calls + 1] = "destroy" end, xf = xf })
    end),
    FinishSpawningActor = ufn("FinishSpawningActor", function() end),
})
local objects = {
    ["/Script/Engine.Default__KismetRenderingLibrary"] = rendering,
    ["/Script/Engine.Default__GameplayStatics"] = statics,
    ["/Script/Engine.SceneCapture2D"] = obj({}),
    ["/Script/Engine.PointLight"] = pointLightClass,
    ["/Script/Engine.Default__CanvasRenderTarget2D"] = canvasRT,
    ["/Script/Engine.CanvasRenderTarget2D"] = obj({}),
}
function StaticFindObject(path)
    return objects[path]
end
function FName(s)
    return s
end

local function actor(name, bones, extra)
    local mesh = obj({
        GetBoneIndex = function(_, n) return bones[n] and 1 or -1 end,
        GetSocketLocation = function(_, n) return { X = 0, Y = 0, Z = 160 } end,
    })
    local a = obj({ name = name, Mesh = mesh })
    a.GetActorForwardVector = function() return { X = 1, Y = 0, Z = 0 } end
    a.K2_GetActorLocation = function() return { X = 0, Y = 0, Z = 90 } end
    for k, v in pairs(extra or {}) do
        a[k] = v
    end
    return a
end

-- Setup -----------------------------------------------------------------------------------
local P = require("uuepbs_portraits")
local folder = dir .. "/_portraits"
local reqFile = dir .. "/portrait_requests.txt"
local profile = {}
local actors = {
    auto = actor("Hero", { head = true }),
    ["7FF1"] = actor("Guard", { Head = true }),
    ["7FF2"] = actor("Boss", {}, { CharacterData = { Portrait = obj({ name = "T_Boss_Portrait" }) } }),
}
local said = {}
P.setup({
    say = function(fmt, ...) said[#said + 1] = string.format(fmt, ...) end,
    chatter = function() end,
    folder = folder,
    request_file = reqFile,
    profile = function() return profile end,
    find_actor = function(id) return actors[id] end,
})
local function requests(text)
    local f = assert(io.open(reqFile, "wb"))
    f:write("UUEPBS portraits 1\n" .. text)
    f:close()
end

-- 1. head shot for the player, exported by the engine
requests("auto\tHero\t1\n7FF1\tAnca\t2\n")
P.tick(10000)
local seq = table.concat(calls, ",")
check(seq:find("rt 256x256 fmt 2", 1, true), "render target RGBA8 256: " .. seq)
check(seq:find("spawn,light,light,showonly Hero,capture,destroy,unlight,unlight", 1, true), "head-shot sequence: " .. seq)
check(seq:find("export Hero.auto.png", 1, true) and seq:find("release", 1, true), "exported and released: " .. seq)
check(captures[1] and captures[1].PrimitiveRenderMode == 2 and captures[1].CaptureSource == 2 and captures[1].bCaptureEveryFrame == false,
    "capture settings (show-only list, LDR, not every frame)")
check(not seq:find("Anca", 1, true), "one picture per tick")

-- 2. the gap: nothing before PORTRAIT_GAP_MS, then the guard (bone "Head")
calls = {}
P.tick(10100)
check(#calls == 0, "waits between pictures")
P.tick(12000)
seq = table.concat(calls, ",")
check(seq:find("export Anca.auto.png", 1, true), "second character: " .. seq)

-- 3. nothing new: idle
calls = {}
P.tick(20000)
check(#calls == 0, "requests already done are not taken again")

-- 4. retake (new nonce) + read-back fallback when export writes nothing
exportWorks = false
requests("auto\tHero\t3\n7FF1\tAnca\t2\n")
P.tick(30000)
seq = table.concat(calls, ",")
check(seq:find("readraw", 1, true), "falls back to reading the pixels: " .. seq)
local f = io.open(folder .. "\\Hero.auto.png", "rb")
local png = f and f:read("a") or ""
if f then f:close() end
check(png:sub(1, 8) == "\137PNG\r\n\26\n" and #png > 256 * 256 * 4, "fallback wrote a PNG (" .. #png .. " bytes)")
exportWorks = true

-- 5. game portrait from the profile
profile.PortraitTexture = { "Missing.Path", "CharacterData.Portrait" }
calls = {}
requests("7FF2\tBoss\t4\n")
P.tick(40000)
seq = table.concat(calls, ",")
check(seq:find("canvas,draw T_Boss_Portrait,endcanvas", 1, true) and not seq:find("spawn", 1, true), "game portrait drawn, no capture: " .. seq)
check(seq:find("export Boss.auto.png", 1, true), "game portrait saved")

-- 6. a character that cannot be taken is given up after two tries (and said once)
spawnWorks = false
profile.PortraitTexture = nil
requests("7FF1\tAnca\t9\n")
P.tick(50000)
P.tick(60000)
P.tick(70000)
check(#said == 1 and said[1]:find("no portrait for Anca", 1, true), "gave up once: " .. table.concat(said, " | "))

-- 7. a character that is not loaded is skipped without counting as a try
calls = {}
requests("DEAD\tGone\t10\n")
P.tick(80000)
check(#calls == 0, "unknown character skipped")

-- 8. preset thumbnails: a whole-body shot saved in _library, before any portrait; old requests are skipped
os.execute('mkdir -p "' .. dir .. '/_library"')
local thumbFolder = dir .. "/_library"
local thumbFile = dir .. "/thumbnail_requests.txt"
local function thumbs(text)
    local f = assert(io.open(thumbFile, "wb"))
    f:write("UUEPBS thumbnails 1\n" .. text)
    f:close()
end
thumbs("auto\tOld preset\t100\n") -- left over from an earlier game session
spawnWorks = true
local function setup(portraitsOn)
    P.setup({
        say = function(fmt, ...) said[#said + 1] = string.format(fmt, ...) end,
        chatter = function() end,
        portraits = portraitsOn,
        folder = folder,
        request_file = reqFile,
        thumb_folder = thumbFolder,
        thumb_request_file = thumbFile,
        profile = function() return profile end,
        find_actor = function(id) return actors[id] end,
    })
end
setup(true)
actors.auto.GetActorBounds = ufn("GetActorBounds", function(_, onlyColliding, originOut, extentOut)
    return { X = 0, Y = 0, Z = 88 }, { X = 30, Y = 30, Z = 90 }
end)
calls, captures = {}, {}
requests("")
P.tick(90000)
seq = table.concat(calls, ",")
check(#calls == 0, "leftover thumbnail requests are not taken: " .. seq)
calls = {}
requests("7FF1\tAnca\t11\n") -- a portrait is waiting too
thumbs("auto\tOld preset\t100\nauto\tCurvy\t101\n")
P.tick(91000)
seq = table.concat(calls, ",")
check(seq:find("spawn,light,light,showonly Hero,capture,destroy,unlight,unlight", 1, true) and seq:find("export Curvy.auto.png", 1, true), "thumbnail taken first: " .. seq)
check(not seq:find("Anca", 1, true), "portrait waits for the next tick")
local cap = captures[1]
check(cap and cap.FOVAngle == 40, "thumbnail field of view")
local tf = io.open(thumbFolder .. "\\Curvy.auto.png", "rb")
check(tf ~= nil, "thumbnail written to _library")
if tf then tf:close() end
-- framed from the bounds: 180 cm tall -> about 2.5 m away
calls = {}
P.tick(93000)
seq = table.concat(calls, ",")
check(seq:find("export Anca.auto.png", 1, true), "portrait after the thumbnail: " .. seq)

-- 9. no bounds: fixed distance from the profile; thumbnails still work with portraits off
actors.auto.GetActorBounds = nil
profile.ThumbnailDistance = 300
setup(false)
calls, captures = {}, {}
requests("7FF1\tAnca\t12\n")
thumbs("auto\tTall legs\t102\n")
P.tick(95000)
seq = table.concat(calls, ",")
check(seq:find("export Tall legs.auto.png", 1, true), "second thumbnail: " .. seq)
calls = {}
P.tick(97000)
check(#calls == 0, "portraits off: no portrait taken")
profile.ThumbnailDistance = nil

-- 10. UE5.3 signatures: the calls are refitted to the longer parameter lists (UE4SS refuses a
-- call with too few arguments, which is what made every portrait fail with "could not create a render target")
sig = UE53
said, calls, captures = {}, {}, {}
setup(true)
requests("auto\tHero\t20\n")
thumbs("")
P.tick(100000)
seq = table.concat(calls, ",")
check(seq:find("rt 256x256 fmt 2", 1, true) and seq:find("spawn,light,light,showonly Hero,capture,destroy,unlight,unlight", 1, true) and seq:find("export Hero.auto.png", 1, true),
    "UE5.3: portrait taken: " .. seq .. " | " .. table.concat(said, " | "))
check(argCounts.CreateRenderTarget2D == 7 and argCounts.BeginDeferredActorSpawnFromClass == 6 and argCounts.FinishSpawningActor == 3,
    "UE5.3: argument lists refitted")
check(#said == 0, "UE5.3: nothing reported: " .. table.concat(said, " | "))

-- 11. an older engine with shorter lists (no bAutoGenerateMipMaps, no bIncludeFromChildActors)
sig = {}
for k, v in pairs(UE4) do
    sig[k] = v
end
sig.CreateRenderTarget2D, sig.ShowOnlyActorComponents = 5, 1
calls = {}
requests("auto\tHero\t21\n")
P.tick(110000)
seq = table.concat(calls, ",")
check(seq:find("export Hero.auto.png", 1, true) and argCounts.CreateRenderTarget2D == 5 and argCounts.ShowOnlyActorComponents == 1,
    "older engine: shorter argument lists: " .. seq)

-- 12. CreateRenderTarget2D unusable: a CanvasRenderTarget2D switched to RGBA8 is used instead
sig = UE53
kismetRT = false
calls = {}
requests("auto\tHero\t22\n")
P.tick(120000)
seq = table.concat(calls, ",")
check(seq:find("canvasrt 256x256,update fmt 2", 1, true) and seq:find("export Hero.auto.png", 1, true),
    "canvas render target fallback: " .. seq)

-- 13. no render target at all: the reason is reported, not just "could not create a render target"
objects["/Script/Engine.Default__CanvasRenderTarget2D"] = nil
P = nil
package.loaded.uuepbs_portraits = nil
P = require("uuepbs_portraits")
said = {}
setup(true)
requests("auto\tHero\t23\n")
P.tick(130000)
P.tick(140000)
check(#said == 1 and said[1]:find("could not create a render target (CreateRenderTarget2D: blocked in this game", 1, true),
    "reason given: " .. table.concat(said, " | "))
kismetRT = true

-- 14. studio lights: key + fill, movable, candela, matched to the sun, gone after the capture
objects["/Script/Engine.Default__CanvasRenderTarget2D"] = canvasRT
sig = UE4
local function shoot(nonce)
    lights, calls, captures, said = {}, {}, {}, {}
    requests("auto\tHero\t" .. nonce .. "\n")
    P.tick(150000 + nonce * 10000)
    return table.concat(calls, ",")
end
seq = shoot(30)
check(#lights == 2, "two studio lights: " .. seq)
local key, fill = lights[1], lights[2]
if key and fill then
    check(key.comp.Mobility == 2 and key.comp.IntensityUnits == 1 and key.comp.CastShadows == false, "key light movable, candela, no shadows")
    check(key.pushed and fill.pushed and key.pushed > fill.pushed * 2, "key brighter than fill: " .. tostring(key.pushed) .. " / " .. tostring(fill.pushed))
    -- head at (0,0,160), camera 60 cm in front: the key sits up and to the side, its intensity gives ~10 lux at the face
    local dx, dy, dz = key.xf.Translation.X, key.xf.Translation.Y, key.xf.Translation.Z - 160
    local r2 = (dx * dx + dy * dy + dz * dz) / 10000
    check(math.abs(key.pushed / r2 - 10) < 0.01, "key matched to a 10 lux sun: " .. tostring(key.pushed / r2))
    check(key.xf.Translation.Z > 170 and key.xf.Translation.X > 50 and math.abs(key.xf.Translation.Y) > 20, "key above and beside the camera")
    check(key.xf.Translation.Y * fill.xf.Translation.Y < 0, "fill on the other side")
    check(key.destroyed and fill.destroyed, "lights removed after the capture")
end
check(seq:find("capture,destroy,unlight,unlight", 1, true), "lights live across the capture: " .. seq)
-- a physically lit level: 100000 lux sun -> 10000x brighter lights
suns[1].Intensity = 100000
suns[2] = { Intensity = 500000, bAffectsWorld = false } -- switched off, ignored
shoot(31)
local k2 = lights[1]
check(k2 and math.abs(k2.pushed / ((k2.xf.Translation.X ^ 2 + k2.xf.Translation.Y ^ 2 + (k2.xf.Translation.Z - 160) ^ 2) / 10000) - 100000) < 1, "matched to a bright sun")
-- no sun: default; profile gain doubles it
suns = {}
profile.PortraitLight = 2
shoot(32)
k2 = lights[1]
check(k2 and math.abs(k2.pushed / ((k2.xf.Translation.X ^ 2 + k2.xf.Translation.Y ^ 2 + (k2.xf.Translation.Z - 160) ^ 2) / 10000) - 20) < 0.01, "no sun: default x gain")
-- off
profile.PortraitLight = false
seq = shoot(33)
check(#lights == 0 and seq:find("spawn,showonly Hero,capture,destroy", 1, true) and seq:find("export Hero.auto.png", 1, true), "studio lights off: " .. seq)
-- no PointLight class (stripped game): the shot is still taken
profile.PortraitLight = nil
objects["/Script/Engine.PointLight"] = nil
P = nil
package.loaded.uuepbs_portraits = nil
P = require("uuepbs_portraits")
setup(true)
seq = shoot(34)
check(#lights == 0 and seq:find("export Hero.auto.png", 1, true) and #said == 0, "no PointLight: unlit shot still saved: " .. seq)

print(failures == 0 and "portraits: all checks passed" or (failures .. " failure(s)"))
os.exit(failures == 0 and 0 or 1)
