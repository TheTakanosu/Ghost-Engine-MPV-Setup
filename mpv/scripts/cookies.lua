-- ==========================================
-- GHOST ENGINE - PORTABLE COOKIE RESOLVER
-- ==========================================
-- mpv hands `ytdl-raw-options` to yt-dlp as literal text, so mpv's own "~~/"
-- shorthand is never expanded there and an absolute path has to be baked in.
-- Hardcoding one in mpv.conf makes the folder machine-specific, and pointing
-- at a cookies.txt that does not exist makes yt-dlp fail on every entry.
--
-- So the path is built here instead: cookies.txt is looked up in mpv's config
-- folders at load time, and cookies are only requested when the file is
-- actually there. find_config_file rather than "~~/": the installer puts
-- everything next to mpv.exe, while "~~/" is %APPDATA%\mpv as soon as that
-- folder exists - and mpv creates it itself.
--
-- Cookies do not last. Each one carries an expiry date, and YouTube also
-- rotates them early whenever the same account is used in the browser. Both
-- end the same way, with songs that will not open, so mpv says which it is:
-- the dates are checked when the first song loads, and the first song that
-- fails is asked again with the cookies and yt-dlp's warnings on.

local utils = require 'mp.utils'

-- node lets yt-dlp solve YouTube's JS challenges, but the stream URLs it gets
-- that way can answer mpv with 403 while the same song plays fine without it
-- (seen with yt-dlp 2026.10.21: every playlist entry failed). Cookies do the
-- same: logged in, YouTube only hands out web-client URLs, which need node and
-- answer 403 unless fetched in ranged chunks the way yt-dlp downloads - mpv
-- asks for the whole file. Measured on one song: without cookies and node it
-- plays, with both mpv gets 403 while yt-dlp downloads it fine.
--
-- So the first try goes without either, which is how almost every song
-- plays, and an entry that failed is retried with cookies and node - an
-- age-restricted song needs both, and gets a URL that does open. ghost.lua
-- replays the entry, this file just remembers which entries failed.
local needs_login = {}

-- The account cookies YouTube signs a session with. Preference and consent
-- cookies expire too, but they do not decide whether a song opens.
local SESSION = {
    SID = true, HSID = true, SSID = true, APISID = true, SAPISID = true,
    LOGIN_INFO = true, ["__Secure-1PSID"] = true, ["__Secure-3PSID"] = true,
}

local HOW_TO_RENEW = "Export a fresh cookies.txt from your browser and put it next to mpv.exe."

local cookies = mp.find_config_file("cookies.txt")
local checked_dates = false
local asked_youtube = false

local function warn(text)
    mp.msg.warn(text)
    mp.osd_message(text .. "\n" .. HOW_TO_RENEW, 10)
end

-- Netscape format: domain, subdomains, path, secure, expiry, name, value.
-- Browsers mark HttpOnly cookies by prefixing the line with "#HttpOnly_";
-- every other line starting with # is a comment.
local function check_dates()
    if checked_dates then return end
    checked_dates = true

    local file = io.open(cookies, "r")
    if not file then return end
    local newest
    for line in file:lines() do
        line = line:gsub("^#HttpOnly_", "")
        local domain, expiry, name = line:match("^([^#\t][^\t]*)\t[^\t]*\t[^\t]*\t[^\t]*\t(%d+)\t([^\t]+)")
        if domain and domain:find("youtube.com", 1, true) and SESSION[name] then
            local at = tonumber(expiry)
            if at and at > 0 and (not newest or at > newest) then newest = at end
        end
    end
    file:close()

    if newest and newest < os.time() then
        warn("Your YouTube cookies expired on " .. os.date("%Y-%m-%d", newest) .. ".")
    end
end

local function current_entry_id()
    local pos = mp.get_property_number("playlist-playing-pos", -1)
    if pos < 0 then return nil end
    return mp.get_property_number("playlist/" .. pos .. "/id")
end

local function resolve_raw_options()
    local options = {}
    if cookies then check_dates() end

    local id = current_entry_id()
    if id and needs_login[id] then
        if cookies then
            options[#options + 1] = "cookies=" .. cookies
            mp.msg.info("retrying with cookies.txt and node")
        else
            mp.msg.info("retrying with node for YouTube's JS challenges")
        end
        options[#options + 1] = "js-runtimes=node"
    end

    mp.set_property("ytdl-raw-options", table.concat(options, ","))
end

-- mpv runs yt-dlp with --no-warnings, which also hides the one that says
-- YouTube no longer accepts these cookies. Ask once more for the failed entry,
-- in the background and only once per session, with warnings left on.
local function ask_youtube_about_cookies(url)
    if asked_youtube or not cookies or not url then return end
    asked_youtube = true
    mp.command_native_async({
        name = "subprocess",
        args = {mp.find_config_file("yt-dlp.exe") or "yt-dlp", "--cookies", cookies,
                "--simulate", "--no-playlist", url},
        capture_stdout = true,
        capture_stderr = true,
        playback_only = false,
    }, function(ok, result)
        local said = ok and result and ((result.stderr or "") .. (result.stdout or "")) or ""
        if said:find("cookies are no longer valid", 1, true) then
            warn("YouTube no longer accepts your cookies.txt.")
        end
    end)
end

local function entry_url(id)
    for _, entry in ipairs(mp.get_property_native("playlist", {})) do
        if entry.id == id and entry.filename then
            return (entry.filename:gsub("^ytdl://", ""))
        end
    end
end

-- yt-dlp only says so while it still recognises the file as an account login,
-- so the plain fact is the dependable signal: a song that failed its first try
-- and both of ghost.lua's retries, which used the cookies.
local GIVE_UP = 3
local failures = {}
local hinted = false

mp.register_event("end-file", function(event)
    if event.reason ~= "error" or not event.playlist_entry_id then return end
    local id = event.playlist_entry_id
    needs_login[id] = true
    ask_youtube_about_cookies(entry_url(id))

    failures[id] = (failures[id] or 0) + 1
    if cookies and not hinted and failures[id] >= GIVE_UP then
        hinted = true
        warn("A song would not open even with retries. If this keeps happening, your cookies.txt has probably expired.")
    end
end)

-- Lower priorities run first. ytdl_hook starts yt-dlp from its own on_load at
-- 10, so anything later only reaches the next file; 5 puts the options in
-- place for the file being opened, which the node retry depends on.
mp.add_hook("on_load", 5, resolve_raw_options)
