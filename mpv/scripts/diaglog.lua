-- A short log of what mpv did, for finding out afterwards why a song or a
-- video was slow or wrong. Off unless mpv.conf turns it on:
--   script-opts-append=ghost-log=C:/path/to/ghost-mpv.log
-- It stays on this computer, starts empty each time mpv opens and stops
-- writing at about 5 MB. mpv's own --log-file would do, but it logs at
-- debug level: 8.5 MB in 45 seconds of one video. This keeps only Ghost's
-- scripts, youtube-dl's and the player's own lines about files and errors.
local path = mp.get_opt("ghost-log")
if not path or path == "" then return end

local file = io.open(path, "w")
if not file then return end
local written = 0
local LIMIT = 5 * 1024 * 1024

local KEEP = { ghost = true, songmatch = true, cookies = true, offline = true, quality = true,
               ytdl_hook = true, updatecheck = true }
local function wanted(prefix, level, text)
    if level == "error" or level == "warn" or level == "fatal" then return true end
    if KEEP[prefix] then return not text:find("EDL", 1, true) end
    -- The player's own: what opens, what ends and why.
    return prefix == "cplayer" and (text:find("^Playing:") or text:find("^Opening done") or
        text:find("finished playback") or text:find("first video frame") or text:find("playback restart"))
end

local function write(line)
    if written > LIMIT then return end
    -- An EDL or a stream URL runs to thousands of characters; the start says enough.
    if #line > 300 then line = line:sub(1, 300) .. "..." end
    file:write(os.date("%H:%M:%S "), line, "\n")
    file:flush()
    written = written + #line + 10
    if written > LIMIT then file:write("-- 5 MB reached, nothing more is written until mpv opens again\n") file:flush() end
end

write("mpv " .. (mp.get_property("mpv-version") or "") .. ", log started")
mp.enable_messages("v")
mp.register_event("log-message", function(e)
    if wanted(e.prefix, e.level, e.text) then
        write(string.format("[%s] %s", e.prefix, (e.text:gsub("%s+$", ""))))
    end
end)
for _, name in ipairs({ "vid", "ytdl-format", "user-data/ghost/audio", "user-data/ghost/video-mode" }) do
    mp.observe_property(name, "string", function(_, value) write("= " .. name .. " " .. tostring(value)) end)
end
mp.register_event("shutdown", function() write("mpv closed") file:close() end)
