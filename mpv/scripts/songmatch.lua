-- ==========================================
-- GHOST ENGINE - SONG MATCHING FOR SEARCH ENTRIES
-- ==========================================
-- A Spotify import has no video ids, so the bot writes every track as
-- `ytdl://ytsearch1:<track> <artist>` and whatever YouTube ranks first is what
-- plays. Now and then that is a live set, a slowed + reverb edit or a cover:
-- in a sample of 150 real entries about one in ten came out that way.
--
-- This picks the song instead. Audio lists ask YouTube Music for songs first,
-- which returns the studio recordings Spotify has; video lists, and audio
-- entries YouTube Music has nothing for, use a normal search of five results.
-- A candidate is taken only when its title contains the track name and it
-- carries no variant word (live, slowed, cover, remix...) the track itself
-- does not have. When nothing qualifies, YouTube's first result plays if it
-- is at least the same song, and the entry is skipped if it is not.
--
-- The next entry is resolved in the background while the current one plays,
-- so in a list this costs no extra wait. Turn it off with GHOST_NO_SONGMATCH=1.

if (os.getenv("GHOST_NO_SONGMATCH") or "") ~= "" then
    return
end

local utils = require 'mp.utils'

local YTDLP = mp.find_config_file("yt-dlp.exe") or "yt-dlp"
local GIVE_UP_AFTER = 15     -- seconds before an unanswered search plays as is
local LONGEST = 15 * 60      -- anything longer is a compilation, not the song

-- Normalised forms (see norm below). A word counts only when the track name
-- does not contain it too, so "Gitme Demem (Akustik)" may still be acoustic.
local VARIANTS = {
    "live", "canli", "concert", "konser", "performance", "performans",
    "slowed", "sped up", "speed up", "speedup", "reverb", "nightcore", "8d",
    "cover", "remix", "acoustic", "akustik", "karaoke", "instrumental",
    "extended", "bass boosted", "1 hour", "1 saat",
}

-- Shortest YouTube name that may stand for a longer Spotify one (see same_artist).
local SHORTEST_NAME = 5

-- A search YouTube answered with nothing like the track: the entry is skipped
-- rather than played as some other song.
local SKIP = {}

local resolved = {}   -- query -> video id, SKIP, or false to play it as is
local pending = {}    -- query -> list of callbacks waiting for that search
local known_as = {}   -- compacted Spotify artist -> shorter name seen on YouTube

-- Lua's lower() only knows ASCII, and Turkish letters are two bytes each.
-- They are folded to plain ASCII rather than lowered: Spotify titles often
-- drop them ("Geri Donme", "Ayse Hatun Onal") while YouTube keeps them.
local TURKISH = {
    ["İ"] = "i", ["I"] = "i", ["ı"] = "i", ["Ş"] = "s", ["ş"] = "s",
    ["Ğ"] = "g", ["ğ"] = "g", ["Ü"] = "u", ["ü"] = "u", ["Ö"] = "o",
    ["ö"] = "o", ["Ç"] = "c", ["ç"] = "c", ["’"] = "'", ["‘"] = "'",
}

local function norm(text)
    text = (text or "")
    for from, to in pairs(TURKISH) do
        text = text:gsub(from, to)
    end
    text = text:lower():gsub("'", ""):gsub("%p", " "):gsub("%s+", " ")
    return " " .. text:match("^%s*(.-)%s*$") .. " "
end

local function has(haystack, needle)
    return haystack:find(needle, 1, true) ~= nil
end

local function compact(text)
    return (norm(text):gsub(" ", ""))
end

-- Whether `names` (titles, channels, credits) name the Spotify artist. Some
-- artists publish under a longer name on Spotify than on YouTube - a second
-- account like "asteriaarchive" whose uploads read "asteria - <song>", or an
-- "X Official" - so a YouTube name the Spotify one starts with counts too.
local function same_artist(names, artist)
    if has(norm(names), norm(artist)) then return true end
    local spotify = compact(artist)
    for name in (names or ""):gmatch("[^,|]+") do
        local short = compact(name)
        if #short >= SHORTEST_NAME and spotify:sub(1, #short) == short then
            -- YouTube's search ranks the shorter name better, so the artist's
            -- next songs are looked up under it too (see resolve).
            known_as[spotify] = name:match("^%s*(.-)%s*$")
            return true
        end
    end
    return false
end

-- "Untouchable (No) - Sped Up" is still "Untouchable": the bare name, without
-- bracketed parts or a " - " suffix, is what a near version has in its title.
local function bare_track(track)
    local bare = track:gsub("%b()", ""):gsub("%b[]", ""):gsub(" %- .*$", "")
    return norm(bare) ~= "  " and norm(bare) or norm(track)
end

-- The artist part of an "<artist> - <track>" upload title, if it has one.
local function title_artist(title)
    return (title or ""):match("^(.-) %- ") or ""
end

local function urlencode(text)
    return (text:gsub("[^%w%-%._~]", function(c)
        return string.format("%%%02X", c:byte())
    end))
end

-- The bot writes `#EXTINF:-1, <artist> - <track>` above `ytsearch1:<track>
-- <artist>`. Splitting the title wherever the query agrees recovers the track
-- and the first credited artist, which is what candidates are compared against.
local function split_title(query, title)
    title = (title or ""):match("^%s*(.-)%s*$")
    local from = 1
    while true do
        local dash = title:find(" - ", from, true)
        if not dash then return nil end
        local artists, track = title:sub(1, dash - 1), title:sub(dash + 3)
        if query == track .. " " .. artists then
            return track, artists:match("^([^,]+)")
        end
        from = dash + 1
    end
end

-- YouTube Music results are songs, so an exact title is the strongest sign.
-- Plain search results are uploads: an exact title there is as often someone's
-- re-upload, and the artist's name in the title or channel matters more.
local function score(entry, index, query, track, artist, songs)
    local title = norm(entry.title)
    local wanted = norm(query)
    local channel = entry.channel or entry.uploader or ""
    for _, word in ipairs(VARIANTS) do
        local padded = " " .. word .. " "
        if has(title, padded) and not has(wanted, padded) then return nil end
    end
    if entry.duration and entry.duration > LONGEST then return nil end

    local name = norm(track)
    if not has(title, name) then return nil end
    local points = 60
    if title == name then points = points + (songs and 40 or 10) end

    if artist and same_artist(table.concat({entry.title or "", title_artist(entry.title), channel}, "|"), artist) then
        points = points + 40
    end
    channel = norm(channel)
    if has(channel, " topic ") then points = points + 15 end
    if has(title, " official audio ") then points = points + 10
    elseif has(title, " official ") then points = points + 5 end
    return points - 2 * index
end

local function pick(entries, query, title, songs)
    local track, artist = split_title(query, title)
    local best, best_points
    for index, entry in ipairs(entries or {}) do
        local points = entry.id and score(entry, index, query, track, artist, songs)
        if points and (not best_points or points > best_points) then
            best, best_points = entry.id, points
        end
    end
    return best
end

local function search(url, done)
    mp.command_native_async({
        name = "subprocess",
        args = {YTDLP, "--no-warnings", "-J", "--flat-playlist", "--playlist-items", "1:5", url},
        capture_stdout = true,
        capture_stderr = true,
        playback_only = false,
    }, function(ok, result)
        local data = ok and result and result.status == 0 and utils.parse_json(result.stdout or "")
        done(data and data.entries or nil)
    end)
end

-- YouTube Music's search list carries titles but no artists, and an artist it
-- does not have still gets someone else's song of the same name. So a pick
-- from there is kept only once the video itself names the expected artist.
-- -J rather than --print: printed text goes out in the console code page and
-- loses Turkish letters, JSON escapes them.
-- With cookies when there are any: an age-restricted song tells nothing about
-- itself to a logged-out request. When it still asks for a login the pick is
-- kept unchecked - it opens with valid cookies, and when they have expired
-- the failed song is what makes cookies.lua say so, where a skip would not.
local function credited(id, artist, done)
    if not artist then done(true) return end
    local args = {YTDLP, "--no-warnings", "-J"}
    local cookies = mp.find_config_file("cookies.txt")
    if cookies then
        args[#args + 1] = "--cookies"
        args[#args + 1] = cookies
    end
    args[#args + 1] = "https://www.youtube.com/watch?v=" .. id
    mp.command_native_async({
        name = "subprocess",
        args = args,
        capture_stdout = true,
        capture_stderr = true,
        playback_only = false,
    }, function(ok, result)
        local info = ok and result and result.status == 0 and utils.parse_json(result.stdout or "")
        if not info then
            done(has(result and result.stderr or "", "confirm your age"))
            return
        end
        done(same_artist(table.concat({info.artist or "", info.uploader or "", info.channel or ""}, "|"), artist))
    end)
end

local function finish(query, id)
    resolved[query] = id or false
    local waiting = pending[query] or {}
    pending[query] = nil
    for _, callback in ipairs(waiting) do callback(id) end
end

local function resolve(query, title, audio, done)
    if resolved[query] ~= nil then
        done(resolved[query] or nil)
        return
    end
    if pending[query] then
        table.insert(pending[query], done)
        return
    end
    pending[query] = {done}

    local track, artist = split_title(query, title)
    -- Without the bot's "<artist> - <track>" title there is nothing to check a
    -- candidate against, and an unchecked pick is worse than YouTube's own.
    if not track then
        finish(query, nil)
        return
    end
    -- Nothing qualified: YouTube's first result still plays when it is the same
    -- song in another version ("bye - super slowed" as a slowed + reverb edit),
    -- and the entry is skipped when it is some other song or there is none.
    -- A search that failed outright leaves the entry to play as before.
    local function fallback(entries)
        if not entries then return false end
        local first = entries[1]
        if first and first.id and has(norm(first.title), bare_track(track)) then
            return first.id
        end
        return SKIP
    end

    local function plain_search()
        search("ytsearch5:" .. query, function(entries)
            local id = pick(entries, query, title, false)
            mp.msg.verbose("youtube search for '" .. query .. "': " .. tostring(id))
            local short = artist and known_as[compact(artist)]
            if id or not short then
                finish(query, id or fallback(entries))
                return
            end
            local terms = track .. " " .. short
            search("ytsearch5:" .. terms, function(again)
                local found = pick(again, query, title, false)
                mp.msg.verbose("youtube search for '" .. terms .. "': " .. tostring(found))
                finish(query, found or fallback(entries))
            end)
        end)
    end

    if not audio then
        plain_search()
        return
    end
    search("https://music.youtube.com/search?q=" .. urlencode(query) .. "#songs", function(entries)
        local id = pick(entries, query, title, true)
        mp.msg.verbose("youtube music for '" .. query .. "': " .. tostring(id))
        if not id then plain_search() return end
        credited(id, artist, function(yes)
            if yes then finish(query, id) return end
            mp.msg.verbose(id .. " is not by " .. artist .. ", searching youtube")
            plain_search()
        end)
    end)
end

local function search_query(path)
    return (path or ""):match("^ytdl://ytsearch%d*:(.+)$")
end

local function entry_title(pos)
    return mp.get_property("playlist/" .. pos .. "/title")
end

local function is_audio_list()
    return mp.get_property_native("user-data/ghost/audio") == true
end

local function watch_url(id)
    return "https://www.youtube.com/watch?v=" .. id
end

-- 8 runs after cookies.lua (5) and before ytdl_hook (10): the URL has to be
-- swapped before yt-dlp is started on the search query.
mp.add_hook("on_load", 8, function(hook)
    -- `path` stays the search query when the URL is swapped here, so the video
    -- actually playing is published for rpc_exporter.lua (Rich Presence needs
    -- its id for the thumbnail and the YouTube button).
    mp.set_property_native("user-data/ghost/source", "")

    local query = search_query(mp.get_property("stream-open-filename"))
    if not query then return end

    -- playlist-playing-pos is the entry being opened. playlist-pos can already
    -- point elsewhere: ghost.lua's retry asks for an index while the next entry
    -- is loading, and that entry's title then met another entry's query.
    local pos = mp.get_property_number("playlist-playing-pos", -1)
    local title = pos >= 0 and entry_title(pos) or nil
    local answered = false

    local function use(id)
        if answered then return end
        answered = true
        -- Moving on to the next entry ends this one without an error, so
        -- ghost.lua does not replay it and cookies.lua does not count it
        -- against the cookies. The last entry has nowhere to go and plays as is.
        if id == SKIP and pos >= 0 and pos + 1 < mp.get_property_number("playlist-count", 0) then
            mp.msg.warn("not found on YouTube, skipping: " .. (title or query))
            mp.osd_message("Not found on YouTube, skipped: " .. (title or query), 4)
            hook:cont()
            mp.commandv("playlist-play-index", tostring(pos + 1))
            return
        end
        if id and id ~= SKIP then
            mp.msg.info("matched '" .. query .. "' to " .. id)
            mp.set_property("stream-open-filename", watch_url(id))
            mp.set_property_native("user-data/ghost/source", watch_url(id))
        end
        hook:cont()
    end

    hook:defer()
    mp.add_timeout(GIVE_UP_AFTER, function() use(nil) end)
    resolve(query, title, is_audio_list(), use)
end)

-- Resolve the next entry while this one plays.
mp.register_event("file-loaded", function()
    local next_pos = mp.get_property_number("playlist-playing-pos", -1) + 1
    local count = mp.get_property_number("playlist-count", 0)
    if next_pos <= 0 or next_pos >= count then return end

    local query = search_query(mp.get_property("playlist/" .. next_pos .. "/filename"))
    if query and resolved[query] == nil and not pending[query] then
        resolve(query, entry_title(next_pos), is_audio_list(), function() end)
    end
end)
