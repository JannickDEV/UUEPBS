-- UUEPBS - character portraits taken inside the game
--
-- The window asks for a portrait of every character it shows without one (native\portrait_requests.txt,
-- written by the DLL). This module takes the picture on the game thread and saves it as
-- "<presets>\_portraits\<name>.auto.png", which the window picks up by itself. A PNG/JPG you put in that
-- folder under the same name (without ".auto") always wins.
--
-- Two ways, tried in this order:
--   1. The game's own portrait: the game profile's "PortraitTexture" names where the character keeps it
--      (property path from the actor, e.g. "CharacterData.FaceIcon"; a list tries each). The texture is
--      drawn into a render target, so this works even when the image only exists on the GPU.
--   2. A head shot: a SceneCapture2D is placed in front of the head bone, renders only that character
--      into a small render target, and is removed again. "PortraitBone", "PortraitDistance" (cm) and
--      "PortraitFov" (degrees) in the game profile tune it.
-- The render target is written with KismetRenderingLibrary::ExportRenderTarget (PNG), or, if that
-- is not available, read back pixel by pixel and encoded here.
--
-- Preset thumbnails (native\thumbnail_requests.txt, written when a preset is saved) use the same
-- machinery with a whole-body shot, saved as "<presets>\_library\<preset>.auto.png". "ThumbnailDistance"
-- (cm) and "ThumbnailFov" (degrees) in the game profile tune the framing; without a distance it is worked
-- out from the character's bounds.
--
-- Everything is guarded: a game where neither works just keeps the monogram tiles. config.lua
-- AutoPortraits = false turns this off. One picture at most every PORTRAIT_GAP_MS, so a crowd does not
-- turn into a stutter.

local P = {}

local ctx = nil -- set by P.setup
local done = {} -- request line -> true (taken or given up)
local tries = {} -- request line -> attempts so far
local nextAt = 0
local found = {}
local PORTRAIT_GAP_MS = 1500
local SIZE = 256
local DEFAULT_BONES = { "head", "Head", "HEAD", "Bip001-Head", "Bip01-Head", "Bip001 Head", "J_Head", "j_head", "Head_M", "head_01", "c_head.x", "def_head" }

local function log(fmt, ...)
    if ctx and ctx.chatter then
        ctx.chatter("portraits: " .. fmt, ...)
    end
end

local function say(fmt, ...)
    if ctx and ctx.say then
        ctx.say("portraits: " .. fmt, ...)
    end
end

local function find(path)
    if found[path] == nil then
        local ok, obj = pcall(StaticFindObject, path)
        found[path] = (ok and obj and obj:IsValid()) and obj or false
    end
    return found[path] or nil
end

local function valid(obj)
    local ok, v = pcall(function()
        return obj ~= nil and obj:IsValid()
    end)
    return ok and v == true
end

-- ---------------------------------------------------------------- PNG (fallback encoder)

local crcTable = nil
local function crc32(s)
    if not crcTable then
        crcTable = {}
        for i = 0, 255 do
            local c = i
            for _ = 1, 8 do
                c = (c & 1) ~= 0 and (0xEDB88320 ~ (c >> 1)) or (c >> 1)
            end
            crcTable[i] = c
        end
    end
    local c = 0xFFFFFFFF
    for i = 1, #s do
        c = crcTable[(c ~ s:byte(i)) & 0xFF] ~ (c >> 8)
    end
    return c ~ 0xFFFFFFFF
end

local function png_chunk(kind, data)
    return string.pack(">I4", #data) .. kind .. data .. string.pack(">I4", crc32(kind .. data))
end

-- rgba: string of w*h*4 bytes. Stored (uncompressed) deflate blocks: small enough at 256x256.
function P.encode_png(w, h, rgba)
    local rows = {}
    for y = 0, h - 1 do
        rows[#rows + 1] = "\0" .. rgba:sub(y * w * 4 + 1, (y + 1) * w * 4)
    end
    local raw = table.concat(rows)
    local z = { "\120\1" }
    local i = 1
    repeat
        local chunk = raw:sub(i, i + 65534)
        local final = (i + 65535 > #raw) and 1 or 0
        z[#z + 1] = string.pack("<BI2I2", final, #chunk, (~#chunk) & 0xFFFF) .. chunk
        i = i + 65535
    until i > #raw
    local a, b = 1, 0
    for k = 1, #raw do
        a = (a + raw:byte(k)) % 65521
        b = (b + a) % 65521
    end
    z[#z + 1] = string.pack(">I4", (b << 16) | a)
    return "\137PNG\r\n\26\n" .. png_chunk("IHDR", string.pack(">I4I4BBBBB", w, h, 8, 6, 0, 0, 0))
        .. png_chunk("IDAT", table.concat(z)) .. png_chunk("IEND", "")
end

-- ---------------------------------------------------------------- engine helpers

local function lib()
    return find("/Script/Engine.Default__KismetRenderingLibrary")
end

local function statics()
    return find("/Script/Engine.Default__GameplayStatics")
end

local function make_target(world)
    local r = lib()
    if not r then
        return nil
    end
    -- RTF_RGBA8 = 2; transparent clear colour; no mips
    local ok, rt = pcall(function()
        return r:CreateRenderTarget2D(world, SIZE, SIZE, 2, { R = 0, G = 0, B = 0, A = 0 }, false)
    end)
    return (ok and valid(rt)) and rt or nil
end

local function release_target(world, rt)
    local r = lib()
    if r and rt then
        pcall(function()
            r:ReleaseRenderTarget2D(rt)
        end)
    end
end

local function file_size(path)
    local f = io.open(path, "rb")
    if not f then
        return 0
    end
    local n = f:seek("end") or 0
    f:close()
    return n
end

local function save_target(world, rt, folder, file)
    local path = folder .. "\\" .. file
    os.remove(path)
    local r = lib()
    if r then
        pcall(function()
            r:ExportRenderTarget(world, rt, folder, file)
        end)
        if file_size(path) > 64 then
            return "exported"
        end
    end
    -- Fallback: read the pixels back and encode here.
    local ok, arr = pcall(function()
        return r:ReadRenderTargetRaw(world, rt, false)
    end)
    if not ok or not arr then
        return nil
    end
    local px = {}
    local okEach = pcall(function()
        arr:ForEach(function(_, elem)
            local c = elem:get()
            local function b(v)
                v = tonumber(v) or 0
                return math.max(0, math.min(255, math.floor(v * 255 + 0.5)))
            end
            px[#px + 1] = string.char(b(c.R), b(c.G), b(c.B), 255)
        end)
    end)
    if not okEach or #px ~= SIZE * SIZE then
        return nil
    end
    local f = io.open(path, "wb")
    if not f then
        return nil
    end
    f:write(P.encode_png(SIZE, SIZE, table.concat(px)))
    f:close()
    return "read back"
end

local function vec(v)
    if not v then
        return nil
    end
    local ok, t = pcall(function()
        return { X = v.X, Y = v.Y, Z = v.Z }
    end)
    return ok and t or nil
end

-- FRotator (degrees) -> FQuat, as UE does it.
local function quat(pitch, yaw, roll)
    local d = math.pi / 360
    local sp, cp = math.sin(pitch * d), math.cos(pitch * d)
    local sy, cy = math.sin(yaw * d), math.cos(yaw * d)
    local sr, cr = math.sin(roll * d), math.cos(roll * d)
    return {
        X = cr * sp * sy - sr * cp * cy,
        Y = -cr * sp * cy - sr * cp * sy,
        Z = cr * cp * sy - sr * sp * cy,
        W = cr * cp * cy + sr * sp * sy,
    }
end

local function head_of(actor)
    local profile = ctx.profile()
    local mesh = nil
    pcall(function()
        mesh = actor.Mesh
    end)
    if not valid(mesh) and ctx.primary_mesh then
        mesh = ctx.primary_mesh(actor)
    end
    local bones = DEFAULT_BONES
    if type(profile.PortraitBone) == "string" and profile.PortraitBone ~= "" then
        bones = { profile.PortraitBone }
    elseif type(profile.PortraitBone) == "table" then
        bones = profile.PortraitBone
    end
    if valid(mesh) then
        for _, b in ipairs(bones) do
            local ok, loc = pcall(function()
                local name = FName(b)
                if mesh:GetBoneIndex(name) < 0 then
                    return nil
                end
                return vec(mesh:GetSocketLocation(name))
            end)
            if ok and loc then
                return loc, b
            end
        end
    end
    local ok, loc = pcall(function()
        return vec(actor:K2_GetActorLocation())
    end)
    if ok and loc then
        loc.Z = loc.Z + 70 -- roughly head height above a capsule's centre
        return loc, "actor + 70 cm"
    end
    return nil
end

-- ---------------------------------------------------------------- the two ways

local function resolve_path(actor, path)
    local obj = actor
    for part in tostring(path):gmatch("[^%.]+") do
        local ok, v = pcall(function()
            return obj[part]
        end)
        if not ok or v == nil then
            return nil
        end
        obj = v
    end
    return valid(obj) and obj or nil
end

local function game_texture(actor)
    local spec = ctx.profile().PortraitTexture
    if spec == nil or spec == "" then
        return nil
    end
    local list = type(spec) == "table" and spec or { spec }
    for _, path in ipairs(list) do
        local tex = resolve_path(actor, path)
        if tex then
            return tex, path
        end
    end
    return nil
end

-- Draws `tex` over the whole render target through a Canvas. The Canvas comes back through out
-- parameters, which UE4SS hands back either as return values or in the tables passed in.
local function draw_texture(world, rt, tex)
    local r = lib()
    if not r then
        return false
    end
    local canvasOut, sizeOut, ctxOut = {}, {}, {}
    local ok, a, b, c = pcall(function()
        return r:BeginDrawCanvasToRenderTarget(world, rt, canvasOut, sizeOut, ctxOut)
    end)
    if not ok then
        log("BeginDrawCanvasToRenderTarget failed: %s", tostring(a))
        return false
    end
    local canvas = (valid(a) and a) or canvasOut.Canvas or canvasOut[1] or (valid(canvasOut) and canvasOut) or nil
    local context = (c ~= nil and c) or ctxOut.Context or ctxOut
    if not valid(canvas) then
        log("no Canvas came back from BeginDrawCanvasToRenderTarget")
        pcall(function()
            r:EndDrawCanvasToRenderTarget(world, context)
        end)
        return false
    end
    local drawn = pcall(function()
        canvas:K2_DrawTexture(tex, { X = 0, Y = 0 }, { X = SIZE, Y = SIZE }, { X = 0, Y = 0 }, { X = 1, Y = 1 }, { R = 1, G = 1, B = 1, A = 1 }, 0, 0,
            { X = 0.5, Y = 0.5 })
    end)
    pcall(function()
        r:EndDrawCanvasToRenderTarget(world, context)
    end)
    return drawn
end

-- Renders only `actor` into `rt` from a camera `dist` cm in front of `centre`, looking back at it.
local function scene_shot(actor, rt, centre, dist, fov, lift, pitch)
    local gs, cls = statics(), find("/Script/Engine.SceneCapture2D")
    if not gs or not cls then
        return false, "no SceneCapture2D / GameplayStatics"
    end
    local fwd = nil
    pcall(function()
        fwd = vec(actor:GetActorForwardVector())
    end)
    fwd = fwd or { X = 1, Y = 0, Z = 0 }
    local cam = { X = centre.X + fwd.X * dist, Y = centre.Y + fwd.Y * dist, Z = centre.Z + lift }
    local yaw = math.deg(math.atan(fwd.Y, fwd.X)) + 180
    local xf = { Rotation = quat(pitch, yaw, 0), Translation = cam, Scale3D = { X = 1, Y = 1, Z = 1 } }

    local cap = nil
    local ok, err = pcall(function()
        cap = gs:BeginDeferredActorSpawnFromClass(actor, cls, xf, 1, nil) -- 1 = always spawn
        if valid(cap) then
            gs:FinishSpawningActor(cap, xf)
        end
    end)
    if not ok or not valid(cap) then
        return false, "could not spawn a SceneCapture2D (" .. tostring(err) .. ")"
    end
    local shot, why = pcall(function()
        local comp = cap.CaptureComponent2D
        comp.TextureTarget = rt
        comp.CaptureSource = 2 -- final colour, LDR
        comp.bCaptureEveryFrame = false
        comp.bCaptureOnMovement = false
        comp.FOVAngle = fov
        comp.PrimitiveRenderMode = 2 -- only what is in the show-only list
        comp:ShowOnlyActorComponents(actor, true)
        comp:CaptureScene()
    end)
    pcall(function()
        cap:K2_DestroyActor()
    end)
    if not shot then
        return false, "capture failed: " .. tostring(why)
    end
    return true
end

local function head_shot(actor, rt)
    local profile = ctx.profile()
    local head, bone = head_of(actor)
    if not head then
        return false, "no head position"
    end
    local ok, why = scene_shot(actor, rt, head, tonumber(profile.PortraitDistance) or 60, tonumber(profile.PortraitFov) or 30, 2, -2)
    if not ok then
        return false, why
    end
    return true, "head shot at " .. tostring(bone)
end

-- The whole character, for preset thumbnails: framed from the actor's bounds when the engine gives
-- them, else from its capsule centre at "ThumbnailDistance" cm (game profile, default 260).
local function body_shot(actor, rt)
    local profile = ctx.profile()
    local fov = tonumber(profile.ThumbnailFov) or 40
    local centre, dist, how = nil, tonumber(profile.ThumbnailDistance), nil
    if not dist then
        local originOut, extentOut = {}, {}
        local ok, a, b = pcall(function()
            return actor:GetActorBounds(true, originOut, extentOut, false)
        end)
        if ok then
            local origin = vec(type(a) == "userdata" and a or nil) or vec(originOut.Origin) or vec(originOut)
            local extent = vec(type(b) == "userdata" and b or nil) or vec(extentOut.BoxExtent) or vec(extentOut)
            local h = extent and tonumber(extent.Z)
            if origin and tonumber(origin.Z) and h and h > 20 and h < 400 then
                centre = origin
                dist = h * 1.12 / math.tan(math.rad(fov) / 2)
                how = string.format("bounds %.0f cm tall", h * 2)
            end
        end
    end
    if not centre then
        local ok, loc = pcall(function()
            return vec(actor:K2_GetActorLocation())
        end)
        if not ok or not loc then
            return false, "no position"
        end
        centre = loc
        centre.Z = centre.Z - 5 -- a capsule's centre sits around the hips; a little lower frames the feet
        dist = dist or 260
        how = string.format("%.0f cm away", dist)
    end
    local ok, why = scene_shot(actor, rt, centre, dist, fov, 10, -3)
    if not ok then
        return false, why
    end
    return true, "body shot, " .. how
end

-- ---------------------------------------------------------------- requests

local function read_requests(file, header)
    local f = file and io.open(file, "rb")
    if not f then
        return nil
    end
    local text = f:read("a")
    f:close()
    local list = {}
    local first = true
    for line in text:gmatch("[^\r\n]+") do
        if first then
            first = false
            if line ~= (header or "UUEPBS portraits 1") then
                return nil
            end
        else
            local id, stem, nonce = line:match("^([^\t]+)\t([^\t]+)\t(%d+)$")
            if id then
                list[#list + 1] = { id = id, stem = stem, nonce = nonce, line = line }
            end
        end
    end
    return list
end

local function take(actor, stem)
    local folder = ctx.folder
    local file = stem .. ".auto.png"
    local rt = make_target(actor)
    if not rt then
        return false, "could not create a render target"
    end
    local how
    local tex, path = game_texture(actor)
    if tex and draw_texture(actor, rt, tex) then
        how = "game portrait (" .. path .. ")"
    else
        local ok, why = head_shot(actor, rt)
        if not ok then
            release_target(actor, rt)
            return false, why
        end
        how = why
    end
    local saved = save_target(actor, rt, folder, file)
    release_target(actor, rt)
    if not saved then
        return false, "could not save the picture (" .. how .. ")"
    end
    return true, string.format("%s: %s, %s", file, how, saved)
end

-- Preset thumbnail: always the whole character (the game's portrait shows a face, not a shape).
local function take_thumbnail(actor, name)
    local file = name .. ".auto.png"
    local rt = make_target(actor)
    if not rt then
        return false, "could not create a render target"
    end
    local ok, how = body_shot(actor, rt)
    if not ok then
        release_target(actor, rt)
        return false, how
    end
    local saved = save_target(actor, rt, ctx.thumb_folder, file)
    release_target(actor, rt)
    if not saved then
        return false, "could not save the picture (" .. how .. ")"
    end
    return true, string.format("thumbnail %s: %s, %s", file, how, saved)
end

local THUMB_HEADER = "UUEPBS thumbnails 1"

-- ctx: { say, chatter, folder, request_file, profile = function, find_actor = function(id), primary_mesh = function(actor),
--        portraits = bool (default true), thumb_folder, thumb_request_file (preset thumbnails; optional) }
function P.setup(c)
    ctx = c
    -- Requests left over from an earlier run are not taken again: they would picture today's shape
    -- under yesterday's preset.
    for _, r in ipairs(read_requests(ctx.thumb_request_file, THUMB_HEADER) or {}) do
        done["thumb\t" .. r.line] = true
    end
end

-- Game thread. Takes at most one picture per call, and not more often than PORTRAIT_GAP_MS.
-- Preset thumbnails go first: someone just pressed Save and is waiting for it.
function P.tick(now)
    if not ctx or now < nextAt then
        return
    end
    if ctx.thumb_folder and ctx.thumb_request_file then
        for _, r in ipairs(read_requests(ctx.thumb_request_file, THUMB_HEADER) or {}) do
            local key = "thumb\t" .. r.line
            if not done[key] then
                local actor = ctx.find_actor(r.id)
                if actor then
                    nextAt = now + PORTRAIT_GAP_MS
                    local ok, info = take_thumbnail(actor, r.stem)
                    tries[key] = (tries[key] or 0) + 1
                    if ok then
                        done[key] = true
                        log("%s", info)
                    elseif tries[key] >= 2 then
                        done[key] = true
                        say("no thumbnail for preset %s: %s", r.stem, tostring(info))
                    end
                    return
                end
            end
        end
    end
    if ctx.portraits == false then
        return
    end
    local list = read_requests(ctx.request_file)
    if not list then
        return
    end
    for _, r in ipairs(list) do
        if not done[r.line] then
            local actor = ctx.find_actor(r.id)
            if actor then
                nextAt = now + PORTRAIT_GAP_MS
                local ok, info = take(actor, r.stem)
                tries[r.line] = (tries[r.line] or 0) + 1
                if ok then
                    done[r.line] = true
                    log("%s", info)
                elseif tries[r.line] >= 2 then
                    done[r.line] = true
                    say("no portrait for %s: %s", r.stem, tostring(info))
                end
                return
            end
        end
    end
end

return P
