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

            if audio then
                mp.set_property("vid", "no")
                mp.set_property("ytdl-format", "bestaudio")
                -- Without a video track mpv opens no window at all, which
                -- leaves an audio list playing with no way to seek, pause or
                -- change the volume except from a terminal nobody has open.
                -- force-window gives it the usual window and on-screen
                -- controls with the cover art in place of the video.
                mp.set_property("force-window", "yes")
                mp.msg.info("Ghost-Audio is online! Thanks for using our system.")
            end
        end
    end
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