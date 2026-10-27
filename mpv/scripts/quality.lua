-- ==========================================
-- GHOST ENGINE - STEP DOWN ON A SLOW CONNECTION
-- ==========================================
-- mpv.conf asks for up to 1080p. On a connection that cannot keep up, a video
-- stops every few seconds to refill its cache, and a bigger cache does not
-- help: there is no speed to fill it with. A lower resolution does, which is
-- what YouTube's own player does too.
--
-- So when a video stalls twice within a minute, the rest of this session asks
-- for 720p, and the video playing is reopened at the same moment. Two more
-- stalls go to 480p, the floor. It never steps back up by itself: a
-- connection that was slow a minute ago is a poor bet for 1080p. Reopening
-- mpv starts at 1080p again.
--
-- Only the "height<=N" in ytdl-format is changed, and only when it is there,
-- so someone who wrote their own format in mpv.conf keeps it. Audio lists
-- fetch audio only and are left alone.
--
-- Turn it off with GHOST_NO_AUTO_QUALITY=1, or by deleting this file.

if (os.getenv("GHOST_NO_AUTO_QUALITY") or "") ~= "" then return end

local STEPS = {720, 480}         -- heights to fall back to, in order
local STALLS = 2                 -- this many stalls ...
local WITHIN = 60                -- ... within this many seconds steps down
local SETTLE = 3                 -- seconds after a seek or start that do not count

local stalls = {}
local quiet_until = 0
local resume_at = nil

local function current_cap()
    return tonumber((mp.get_property("ytdl-format", "")):match("height<=(%d+)"))
end

local function next_step(cap)
    for _, height in ipairs(STEPS) do
        if height < cap then return height end
    end
end

local function step_down()
    local cap = current_cap()
    local lower = cap and next_step(cap)
    if not lower then return end

    local format = mp.get_property("ytdl-format"):gsub("height<=%d+", "height<=" .. lower)
    mp.set_property("ytdl-format", format)
    stalls = {}
    mp.msg.warn("slow connection, asking for " .. lower .. "p from now on")
    mp.osd_message("Slow connection: switching to " .. lower .. "p", 4)

    -- Reopen what is playing at the same moment, so the drop helps now and
    -- not only from the next video.
    local pos = mp.get_property_number("time-pos")
    local index = mp.get_property_number("playlist-pos", -1)
    if pos and index >= 0 then
        resume_at = pos
        mp.commandv("playlist-play-index", tostring(index))
    end
end

local function quiet_for(seconds)
    quiet_until = mp.get_time() + seconds
end

mp.observe_property("paused-for-cache", "bool", function(_, waiting)
    if not waiting or mp.get_time() < quiet_until then return end
    if mp.get_property_native("user-data/ghost/audio") == true then return end
    if mp.get_property("vid", "no") == "no" then return end

    local now = mp.get_time()
    local recent = {}
    for _, at in ipairs(stalls) do
        if now - at <= WITHIN then recent[#recent + 1] = at end
    end
    recent[#recent + 1] = now
    stalls = recent
    mp.msg.verbose("stalled for cache (" .. #stalls .. " in the last " .. WITHIN .. " s)")
    if #stalls >= STALLS then step_down() end
end)

-- A seek or a new file empties the cache on purpose; that wait is not the
-- connection's fault.
mp.register_event("seek", function() quiet_for(SETTLE) end)
mp.register_event("start-file", function() quiet_for(SETTLE) end)
mp.register_event("file-loaded", function()
    quiet_for(SETTLE)
    if resume_at then
        mp.commandv("seek", tostring(resume_at), "absolute")
        resume_at = nil
    end
end)
