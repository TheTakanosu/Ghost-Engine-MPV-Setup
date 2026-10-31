-- What the Ghost Engine app's settings page chose, read on every list so a
-- change applies from the next one. mpv.conf (script-opts-append=...) or the
-- app, for the window, sets them; unset means the old behaviour.
--   ghost-audio=standard|saver   a smaller audio stream for song lists
--   ghost-window=no              song lists play with no mpv window: the app
--                                is the player (only the app passes this, so
--                                a list double-clicked in Explorer still
--                                gets a window to stop it from)
local AUDIO_FORMATS = {
    standard = "bestaudio[abr<=130]/bestaudio",
    saver = "bestaudio[abr<=70]/worstaudio/bestaudio",
}

-- A song list switches mpv to sound only: no video track, an audio-only
-- format, a window forced open (or not). Those settings outlive the list,
-- so a video list opened after it in the same mpv played as sound only, with
-- no picture and no window. The video settings in force before are kept
-- here and put back when a list without #GHOST_AUDIO opens. The app, when
-- its video quality changes while a song list plays, passes the new format
-- as ghost-video-format so it is the one put back.
local video_format = mp.get_property("ytdl-format")
local in_audio_mode = false

mp.add_hook("on_load", 50, function()
    local path = mp.get_property("path", "")
    
    if string.match(path, "%.m3u$") then
        local file = io.open(path, "r")
        if file then
            local content = file:read(256)
            file:close()

            local audio = content and string.match(content, "#GHOST_AUDIO") ~= nil
            -- songmatch.lua asks YouTube Music for songs in audio lists.
            mp.set_property_native("user-data/ghost/audio", audio or false)
            mp.set_property_native("user-data/ghost/video-mode", false)
            mp.set_property_native("user-data/ghost/video-once", "")

            if audio then
                if not in_audio_mode then
                    video_format = mp.get_property("ytdl-format")
                    in_audio_mode = true
                end
                mp.set_property("vid", "no")
                mp.set_property("ytdl-format", AUDIO_FORMATS[mp.get_opt("ghost-audio") or ""] or "bestaudio")
                -- Without a video track mpv opens no window at all, which
                -- leaves an audio list playing with no way to seek, pause or
                -- change the volume except from a terminal nobody has open.
                -- force-window gives it the usual window and on-screen
                -- controls with the cover art in place of the video.
                mp.set_property("force-window", mp.get_opt("ghost-window") == "no" and "no" or "yes")
                mp.msg.info("Ghost-Audio is online! Thanks for using our system.")
            elseif in_audio_mode then
                mp.set_property("vid", "auto")
                mp.set_property("ytdl-format", mp.get_opt("ghost-video-format") or video_format)
                mp.set_property("force-window", "no")
                in_audio_mode = false
                mp.msg.info("video list: picture and window are back")
            end
        end
    end
end)

-- The app's now playing view can switch a song list to videos ("Switch to
-- video"): from then on each song plays as its video, picture included,
-- until "Switch to audio" or another list. songmatch.lua looks the songs up
-- on YouTube instead of YouTube Music meanwhile. 9: after songmatch.lua (8)
-- has picked the video, before ytdl_hook (10) opens it.
--
-- "Mini video" is the same for one song only: the entry is named in
-- user-data/ghost/video-once, and the next one plays as sound again.
-- Named here, from mpv's own `path`, at the app's word: the app's copy of
-- `path` could still be the .m3u's just after a list began, and a name
-- that matched no entry was forgotten at once, the song staying sound.
mp.register_script_message("ghost-video-once", function(on)
    local pos = mp.get_property_number("playlist-pos", -1)
    if pos < 0 then return end
    mp.set_property_native("user-data/ghost/video-once", on == "yes" and mp.get_property("path", "") or "")
    local at = mp.get_property_number("time-pos")
    if at then mp.set_property("start", string.format("%.1f", at)) end
    mp.commandv("playlist-play-index", tostring(pos))
end)

mp.add_hook("on_load", 9, function()
    local once = mp.get_property_native("user-data/ghost/video-once")
    local single = type(once) == "string" and once ~= "" and once == mp.get_property("path", "")
    if not single and type(once) == "string" and once ~= "" then
        mp.set_property_native("user-data/ghost/video-once", "")
    end
    local mode = mp.get_property_native("user-data/ghost/video-mode") == true
    local on = mp.get_property_native("user-data/ghost/audio") == true and (mode or single)
    if on then
        mp.set_property("file-local-options/vid", "auto")
        local format = mp.get_property_native("user-data/ghost/video-format") or ""
        if format ~= "" then
            mp.set_property("file-local-options/ytdl-format", format)
        end
    end
    -- Before video-on: the app reads both when video-on changes.
    mp.set_property_native("user-data/ghost/video-panel", on and single and not mode)
    mp.set_property_native("user-data/ghost/video-on", on)
end)

-- Opening a song means a YouTube search and an extraction, 3-4 s on a good
-- connection and over 20 s on a slow laptop, and mpv shows nothing meanwhile.
-- An unchanged window reads as a frozen player, so say what is happening.
-- Only once it has taken over a second: a song already resolved opens
-- without a flash of text. It is an overlay of its own rather than an OSD
-- message, so it never covers the cookie or "skipped" notices the other
-- scripts put up meanwhile.
local loading_timer
local loading = mp.create_osd_overlay("ass-events")

local function stop_loading()
    if loading_timer then loading_timer:kill() loading_timer = nil end
    loading:remove()
end

local function ass_escape(text)
    return (text:gsub("\\", "\\\226\129\160"):gsub("{", "\\{"):gsub("\n", " "))
end

mp.register_event("start-file", function()
    stop_loading()
    local pos = mp.get_property_number("playlist-playing-pos", -1)
    local title = pos >= 0 and mp.get_property("playlist/" .. pos .. "/title") or nil
    loading_timer = mp.add_timeout(1, function()
        loading_timer = nil
        -- Top left: the bottom belongs to the on-screen controls.
        loading.data = "{\\an7\\fs28\\bord2}Loading" ..
            (title and (": " .. ass_escape(title:match("^%s*(.-)%s*$"))) or "") .. " ..."
        loading:update()
    end)
end)
mp.register_event("file-loaded", stop_loading)
mp.register_event("end-file", stop_loading)

-- YouTube now and then answers a freshly signed stream URL with 403 even
-- though the next extraction of the same song plays fine. mpv treats that as
-- a broken file and jumps to the next entry, so songs seemed to skip at
-- random. Give each entry a couple of fresh extractions before moving on.
local MAX_RETRIES = 2
local retries = {}

mp.register_event("end-file", function(event)
    if event.reason ~= "error" or not event.playlist_entry_id then return end
    local id = event.playlist_entry_id
    local tried = retries[id] or 0
    if tried >= MAX_RETRIES then return end

    for index, entry in ipairs(mp.get_property_native("playlist", {})) do
        if entry.id == id then
            retries[id] = tried + 1
            mp.msg.warn(string.format("Could not open this song, trying again (%d/%d)", tried + 1, MAX_RETRIES))
            mp.commandv("playlist-play-index", tostring(index - 1))
            return
        end
    end
end)