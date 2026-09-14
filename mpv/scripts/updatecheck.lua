-- ==========================================
-- GHOST ENGINE - yt-dlp FRESHNESS CHECK
-- ==========================================
-- yt-dlp is the part that actually resolves a YouTube link, and it breaks
-- whenever YouTube changes its player. Fixes ship within days. An install left
-- alone for a few weeks therefore stops playing videos with no explanation,
-- and the obvious conclusion for the person using it is that *this* is broken.
--
-- So mpv says so. Once a day at most, in the background, it compares the
-- yt-dlp next to it against the latest release and puts a line on screen if it
-- has fallen behind. It never downloads anything: an update is one
-- double-click on update.bat, and it stays the user's decision. A player that
-- silently replaces an executable on your disk because you opened a video is
-- not a trade anyone agreed to.
--
-- Nothing here blocks playback. Both steps are async subprocesses and the
-- message, if there is one, arrives while the track is already playing.
--
-- Turn it off with GHOST_NO_UPDATE_CHECK=1, or by deleting this file.

local CHECK_EVERY = 24 * 60 * 60          -- seconds between checks
local API = "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest"
local STAMP = mp.command_native({"expand-path", "~~/.ytdlp-checked"})

if (os.getenv("GHOST_NO_UPDATE_CHECK") or "") ~= "" then
    return
end

local function exists(path)
    local file = io.open(path, "r")
    if file then file:close() return true end
    return false
end

-- The portable install keeps yt-dlp next to mpv. Falling back to the bare name
-- lets a system-wide yt-dlp on PATH be checked too.
local YTDLP = mp.command_native({"expand-path", "~~/yt-dlp.exe"})
if not exists(YTDLP) then YTDLP = "yt-dlp" end

local function checked_recently()
    local file = io.open(STAMP, "r")
    if not file then return false end
    local written = tonumber(file:read("*l") or "") or 0
    file:close()
    return (os.time() - written) < CHECK_EVERY
end

local function remember_check()
    local file = io.open(STAMP, "w")
    if file then
        file:write(tostring(os.time()))
        file:close()
    end
end

local function run(args, done)
    mp.command_native_async({
        name = "subprocess",
        args = args,
        capture_stdout = true,
        capture_stderr = true,
        playback_only = false,   -- keep going across track changes
    }, function(ok, result)
        if ok and result and result.status == 0 then
            done(result.stdout or "")
        end
    end)
end

local function check()
    run({YTDLP, "--version"}, function(out)
        local installed = out:gsub("%s+", "")
        if installed == "" then return end

        -- curl ships with Windows 10 1803 and later, the same release that
        -- brought tar.exe. No extra dependency for either.
        run({"curl", "-sL", "--max-time", "8", "-H", "User-Agent: ghost-mpv", API}, function(body)
            local latest = body:match('"tag_name"%s*:%s*"([^"]+)"')
            if not latest then return end

            remember_check()
            if latest == installed then
                mp.msg.info("yt-dlp is current (" .. installed .. ").")
                return
            end

            mp.msg.warn("yt-dlp " .. installed .. " is behind " .. latest .. ".")
            mp.osd_message(
                "yt-dlp is out of date (" .. installed .. " → " .. latest .. ")\n" ..
                "Videos may stop resolving. Run update.bat in your mpv folder.",
                10)
        end)
    end)
end

-- Waiting for the first file keeps the check off mpv's startup path entirely,
-- and means an idle mpv that never plays anything never asks GitHub anything.
local started = false
mp.register_event("file-loaded", function()
    if started then return end
    started = true
    if not checked_recently() then check() end
end)
