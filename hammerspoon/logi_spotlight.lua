-- Spotlight overlay driven by the Logitech Spotlight 2.
--
-- bin/helper holds the remote, diverts its buttons and streams presses over a
-- unix socket. This dims the screen and punches a bright circle around the
-- cursor while the trigger button is held.
--
-- The circle follows the system cursor rather than gyro deltas, because the
-- remote already moves the cursor itself while a pointing control is down.
--
-- Install: symlink into ~/.hammerspoon/modules/ and require it from init.lua.
--   require("modules.logi_spotlight").start()

local M = {}

local SOCKET = "/tmp/logi-spotlight.sock"

-- Which button shows the spotlight. See docs/PROTOCOL.md for the map.
--   00d8  big round button on the top face, press and hold
--   00fc  side button on the right edge, press and hold
-- Motion has only been observed while 00d8 is held, so start there.
local TRIGGER_CID = "00d8"

local RADIUS = 120       -- points
local DIM = 0.75         -- 0 is clear, 1 is black
local FOLLOW_HZ = 60
-- 1 shows live content through the hole. Above 1 magnifies a snapshot taken
-- when the button went down, which is why the view under the circle does not
-- update while you hold it.
--
-- Anything above 1 needs Hammerspoon granted Screen Recording, in System
-- Settings, Privacy & Security. Without it screen:snapshot() returns an empty
-- image and the circle draws flat grey. The plain hole at 1 needs no
-- permission at all.
local MAGNIFY = 2.0

local sock, canvas, follow
local lastRx = 0
local shown = false

-- ============================================================
-- OVERLAY
-- ============================================================

-- Built per show, on whichever screen the cursor is on, since that is not
-- knowable until the button goes down.
local function build()
    local screen = hs.mouse.getCurrentScreen() or hs.screen.mainScreen()
    local frame = screen:fullFrame()

    -- Taken before the canvas exists. Snapshotting once the overlay is up
    -- captures the dim layer and the magnifier feeds on its own output.
    local base = MAGNIFY > 1 and screen:snapshot() or nil

    local c = hs.canvas.new(frame)
    c[1] = {
        type = "rectangle",
        action = "fill",
        fillColor = { red = 0, green = 0, blue = 0, alpha = DIM },
    }
    if base then
        -- Clip to the circle, then lay the enlarged snapshot over the dim
        -- layer inside it.
        c[2] = { type = "circle", action = "clip",
                 center = { x = 0, y = 0 }, radius = RADIUS }
        c[3] = { type = "image", image = base, imageScaling = "scaleToFit",
                 frame = { x = 0, y = 0, w = frame.w, h = frame.h } }
        c[4] = { type = "resetClip" }
    else
        -- destinationOut removes what is already drawn wherever this shape is
        -- opaque, so the circle is a hole in the dim layer showing live
        -- content rather than a disc on top of it.
        c[2] = {
            type = "circle",
            action = "fill",
            fillColor = { white = 1, alpha = 1 },
            compositeRule = "destinationOut",
            center = { x = 0, y = 0 },
            radius = RADIUS,
        }
    end
    c:level(hs.canvas.windowLevels.screenSaver)
    c:behavior(hs.canvas.windowBehaviors.canJoinAllSpaces)
    -- Clicks must reach whatever is underneath, or the overlay would swallow
    -- every interaction while it is up.
    c:clickActivating(false)
    c:canvasMouseEvents(false, false, false, false)
    return c, frame
end

-- Scaling the whole snapshot and sliding it under the circle costs two
-- subtractions per frame. Cropping a fresh image each frame would allocate one
-- instead, sixty times a second.
local function moveTo(frame)
    local p = hs.mouse.absolutePosition()
    local x, y = p.x - frame.x, p.y - frame.y
    canvas[2].center = { x = x, y = y }
    if MAGNIFY > 1 then
        -- Place the enlarged image so the point under the cursor lands at the
        -- centre of the circle.
        canvas[3].frame = {
            x = x - x * MAGNIFY,
            y = y - y * MAGNIFY,
            w = frame.w * MAGNIFY,
            h = frame.h * MAGNIFY,
        }
    end
end

local function show()
    if shown then return end
    shown = true
    local frame
    canvas, frame = build()
    moveTo(frame)
    canvas:show()
    follow = hs.timer.new(1 / FOLLOW_HZ, function()
        if canvas then moveTo(frame) end
    end)
    follow:start()
end

local function hide()
    if not shown then return end
    shown = false
    if follow then follow:stop() follow = nil end
    if canvas then canvas:delete() canvas = nil end
end

-- ============================================================
-- HELPER SOCKET
-- ============================================================

local function onLine(line)
    lastRx = os.time()
    if line == "ping" or line == "ready" then return end

    local cid = line:match("^down (%x+)$")
    if cid then
        if cid == TRIGGER_CID then show() end
        return
    end
    if line == "up" then hide() end
end

-- hs.socket reads one delimiter at a time, so each callback queues the next.
-- Queueing inline re-enters this callback while buffered data is pending and
-- blows the stack, so the next read is deferred by a tick. A read completing
-- with no data means the peer is gone, and re-arming on that pins the main
-- thread at 100%, because the next read completes empty too.
local function onRead(data)
    if not data or #data == 0 then
        if sock then pcall(function() sock:disconnect() end) end
        sock = nil
        hide()
        return
    end
    onLine((data:gsub("\n$", "")))
    hs.timer.doAfter(0, function()
        if sock then sock:read("\n") end
    end)
end

-- Idempotent. A second live socket in the same Lua state delivers every event
-- twice, which here would show and immediately re-show the overlay.
local function connect()
    if sock then
        sock:disconnect()
        sock = nil
    end
    local s = hs.socket.new(onRead)
    sock = s
    s:connect(SOCKET, function()
        if sock ~= s then s:disconnect() return end
        print("[Spotlight] connected")
        s:read("\n")
    end)
end

-- sock:connected() keeps reporting true after the link is gone, so silence is
-- the only reliable signal. The helper pings every 10 s.
local function ensureConnected()
    if not hs.fs.attributes(SOCKET) then return end
    local now = os.time()
    if sock and sock:connected() and (now - lastRx) < 30 then return end
    if sock then pcall(function() sock:disconnect() end) end
    sock = nil
    lastRx = now
    connect()
end

function M.start()
    lastRx = os.time()
    if not M.timer then
        M.timer = hs.timer.new(5, ensureConnected)
        M.timer:start()
    end
    ensureConnected()
    return M
end

function M.stop()
    if M.timer then M.timer:stop() M.timer = nil end
    if sock then pcall(function() sock:disconnect() end) sock = nil end
    hide()
end

-- Shows the overlay for a few seconds with no device, to check the drawing.
function M.preview(secs)
    show()
    hs.timer.doAfter(secs or 3, hide)
end

return M
