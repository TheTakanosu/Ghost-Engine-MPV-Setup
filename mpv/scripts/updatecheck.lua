-- ==========================================
-- GHOST ENGINE - UPDATE CHECK (yt-dlp and Ghost Engine)
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

-- The portable install keeps yt-dlp next to mpv. find_config_file looks there
-- too, where "~~/" would only mean %APPDATA%\mpv once that folder exists.
-- Falling back to the bare name lets a system-wide yt-dlp on PATH be checked.
local YTDLP = mp.find_config_file("yt-dlp.exe") or "yt-dlp"

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
        -- A failure answers with nothing, so check() still hears back.
        done(ok and result and result.status == 0 and result.stdout or "")
    end)
end

-- Ghost Engine's own version is in a VERSION file in the install folder.
-- Installs from before 1.2.5 have none and are not checked: their update.bat
-- only knows yt-dlp, so pointing them at it would not help.
local VERSION_FILE = mp.find_config_file("VERSION")
local GHOST_API = "https://api.github.com/repos/TheTakanosu/Ghost-Engine-MPV-Setup/releases/latest"

local function read_version()
    if not VERSION_FILE then return nil end
    local file = io.open(VERSION_FILE, "r")
    if not file then return nil end
    local text = (file:read("*l") or ""):gsub("%s+", "")
    file:close()
    return text ~= "" and text or nil
end

-- "1.2.10" > "1.2.9": compared number by number, not as text. A release is
-- newer than its own pre-release ("1.3.0" > "1.3.0-beta.2"); latest is never
-- a pre-release here.
local function newer(latest, installed)
    local core_a = latest:match("^[%d.]+") or ""
    local core_b, pre_b = installed:match("^([%d.]+)(.*)$")
    local a, b = {}, {}
    for n in core_a:gmatch("%d+") do a[#a + 1] = tonumber(n) end
    for n in (core_b or ""):gmatch("%d+") do b[#b + 1] = tonumber(n) end
    for i = 1, math.max(#a, #b) do
        local x, y = a[i] or 0, b[i] or 0
        if x ~= y then return x > y end
    end
    return (pre_b or "") ~= ""
end

local function check()
    -- Both answers arrive on their own; the notice waits for the pair so one
    -- message does not wipe the other off the screen.
    local lines, waiting = {}, 2
    local function done(line)
        if line then lines[#lines + 1] = line end
        waiting = waiting - 1
        if waiting > 0 or #lines == 0 then return end
        mp.osd_message(table.concat(lines, "\n") .. "\nRun update.bat in your mpv folder.", 10)
    end

    run({YTDLP, "--version"}, function(out)
        local installed = out:gsub("%s+", "")
        if installed == "" then done() return end

        -- curl ships with Windows 10 1803 and later, the same release that
        -- brought tar.exe. No extra dependency for either.
        run({"curl", "-sL", "--max-time", "8", "-H", "User-Agent: ghost-mpv", API}, function(body)
            local latest = body:match('"tag_name"%s*:%s*"([^"]+)"')
            if not latest then done() return end

            remember_check()
            if latest == installed then
                mp.msg.info("yt-dlp is current (" .. installed .. ").")
                done()
                return
            end

            mp.msg.warn("yt-dlp " .. installed .. " is behind " .. latest .. ".")
            done("yt-dlp is out of date (" .. installed .. " → " .. latest .. ")\n" ..
                 "Videos may stop resolving.")
        end)
    end)

    local installed = read_version()
    if not installed then done() return end
    -- releases/latest never returns a pre-release, so test builds are not
    -- announced here; update.bat offers them when asked.
    run({"curl", "-sL", "--max-time", "8", "-H", "User-Agent: ghost-mpv", GHOST_API}, function(body)
        local latest = body:match('"tag_name"%s*:%s*"v?([^"]+)"')
        if not latest or not newer(latest, installed) then done() return end
        mp.msg.warn("Ghost Engine " .. installed .. " is behind " .. latest .. ".")
        done("Ghost Engine " .. latest .. " is out (you have " .. installed .. ").")
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
