-- Live YouTube streams with the sound in step with the picture.
--
-- For a stream on air YouTube gives the picture and the sound as two HLS
-- playlists. ytdl_hook opens them as two streams side by side (edl://),
-- each from its own live edge read a moment apart: one could start a
-- segment (2-5 s) after the other and the sound stayed that far off, while
-- mpv, timing each stream on its own, saw nothing wrong (Batin, 2026-11-09,
-- Jynxzi's stream). The master playlist holds both, read by one demuxer
-- from the same segment numbers, so they start together. Videos that are
-- not live are left as they are.

local utils = require "mp.utils"

-- The highest picture under the quality cap ("height<=1080" in ytdl-format),
-- as a bit rate for mpv's hls-bitrate.
local function bitrate_for(formats)
    local cap = tonumber(string.match(mp.get_property("ytdl-format", ""), "height<=(%d+)")) or 100000
    local best = 0
    for _, f in ipairs(formats) do
        if f.vcodec and f.vcodec ~= "none" and (f.height or 0) <= cap and (f.tbr or 0) > best then
            best = f.tbr
        end
    end
    return best > 0 and math.floor(best * 1000) or nil
end

-- A stream played as sound, opened with its picture (see below) until it
-- plays.
local sound_only = false

-- The app shows "LIVE" and "Go live" by this.
mp.register_event("start-file", function()
    mp.set_property_native("user-data/ghost/live", false)
    sound_only = false
end)

mp.register_event("playback-restart", function()
    if sound_only then
        sound_only = false
        mp.set_property("vid", "no")
    end
end)

-- After ytdl_hook (on_load, 10): it has asked yt-dlp and left the answer.
mp.add_hook("on_load", 11, function()
    local raw = mp.get_property("user-data/mpv/ytdl/json-subprocess-result", "")
    if raw == "" then
        return
    end
    local result = utils.parse_json(raw)
    if not result or result.status ~= 0 or not result.stdout then
        return
    end
    local info = utils.parse_json(result.stdout)
    if not info or not info.is_live or not info.id then
        return
    end
    -- The answer is kept from file to file: only this file's own.
    local path = mp.get_property("path", "")
    if not string.find(path, info.id, 1, true) then
        return
    end
    mp.set_property_native("user-data/ghost/live", true)
    -- A stream on air opens at what is on air now, never at a second kept
    -- from before ("pick up where you left off" asked for 1:00:59 of an
    -- hour long window and the picture stayed black).
    mp.set_property("file-local-options/start", "none")
    -- Played as sound (a song list, a radio found in search; the option,
    -- as no track is chosen yet while loading). With the picture off from
    -- the start, mpv took the sound's track only after lavf had read some of
    -- it, and its refresh seek went to the first segment of the hour YouTube
    -- keeps: a radio played from an hour ago. The sound's own playlist did
    -- the same. Opened with the picture, both tracks are taken at once and
    -- it starts at the live edge; the picture goes off once it plays.
    local vid = mp.get_property("file-local-options/vid", "")
    if vid == "" then vid = mp.get_property("options/vid", "auto") end
    local as_sound = vid == "no"
    if as_sound then
        mp.set_property("file-local-options/vid", "auto")
        sound_only = true
    end
    local manifest
    for _, f in ipairs(info.formats or {}) do
        if f.manifest_url and string.find(f.protocol or "", "m3u8", 1, true) then
            manifest = f.manifest_url
            break
        end
    end
    if not manifest then
        return
    end
    -- As sound, a small picture (about 360p, whose sound is the full one;
    -- the smallest carry a thinner sound): it is read but never shown.
    local rate = as_sound and 1000000 or bitrate_for(info.formats or {})
    if rate then
        mp.set_property("file-local-options/hls-bitrate", tostring(rate))
    end
    mp.msg.info("live stream: the HLS master playlist, sound and picture together" ..
                (rate and (" (up to " .. rate .. " b/s)") or ""))
    mp.set_property("stream-open-filename", manifest)
end)
