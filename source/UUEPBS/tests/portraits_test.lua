-- Host test for Scripts/uuepbs_portraits.lua with a mocked engine (no game needed).
--   lua portraits_test.lua ../../../mod/UUEPBS/Scripts /tmp/uuepbs_portraits
-- Checks: request file parsing, one picture per gap, the head-shot call sequence (spawn a
-- SceneCapture2D, show only the character, capture, export, destroy), the game-portrait path,
-- the read-back PNG fallback, and that a failing character is given up after two tries.
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
local exportWorks = true
local spawnWorks = true
local captures = {}
local rendering = obj({
    CreateRenderTarget2D = function(_, world, w, h, fmt)
        calls[#calls + 1] = "rt " .. w .. "x" .. h .. " fmt " .. fmt
        return obj({ kind = "rt" })
    end,
    ReleaseRenderTarget2D = function()
        calls[#calls + 1] = "release"
    end,
    ExportRenderTarget = function(_, world, rt, folder, file)
        calls[#calls + 1] = "export " .. file
        if exportWorks then
            local f = assert(io.open(folder .. "\\" .. file, "wb"))
            f:write(string.rep("x", 100))
            f:close()
        end
    end,
    ReadRenderTargetRaw = function()
        calls[#calls + 1] = "readraw"
        return {
            ForEach = function(_, fn)
                for i = 1, 256 * 256 do
                    fn(i, { get = function() return { R = 0.5, G = 0.25, B = 1, A = 1 } end })
                end
            end,
        }
    end,
    BeginDrawCanvasToRenderTarget = function(_, world, rt, canvasOut, sizeOut, ctxOut)
        calls[#calls + 1] = "canvas"
        return obj({ K2_DrawTexture = function(_, tex) calls[#calls + 1] = "draw " .. tex.name end }), { X = 256, Y = 256 }, { ctx = true }
    end,
    EndDrawCanvasToRenderTarget = function()
        calls[#calls + 1] = "endcanvas"
    end,
})
local statics = obj({
    BeginDeferredActorSpawnFromClass = function(_, world, cls, xf)
        calls[#calls + 1] = "spawn"
        if not spawnWorks then
            return nil
        end
        local comp = obj({
            ShowOnlyActorComponents = function(_, actor) calls[#calls + 1] = "showonly " .. actor.name end,
            CaptureScene = function(self)
                calls[#calls + 1] = "capture"
                captures[#captures + 1] = self
            end,
        })
        return obj({ CaptureComponent2D = comp, K2_DestroyActor = function() calls[#calls + 1] = "destroy" end, xf = xf })
    end,
    FinishSpawningActor = function() end,
})
local objects = {
    ["/Script/Engine.Default__KismetRenderingLibrary"] = rendering,
    ["/Script/Engine.Default__GameplayStatics"] = statics,
    ["/Script/Engine.SceneCapture2D"] = obj({}),
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
check(seq:find("spawn,showonly Hero,capture,destroy", 1, true), "head-shot sequence: " .. seq)
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
actors.auto.GetActorBounds = function(_, onlyColliding, originOut, extentOut)
    return { X = 0, Y = 0, Z = 88 }, { X = 30, Y = 30, Z = 90 }
end
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
check(seq:find("spawn,showonly Hero,capture,destroy", 1, true) and seq:find("export Curvy.auto.png", 1, true), "thumbnail taken first: " .. seq)
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

print(failures == 0 and "portraits: all checks passed" or (failures .. " failure(s)"))
os.exit(failures == 0 and 0 or 1)
