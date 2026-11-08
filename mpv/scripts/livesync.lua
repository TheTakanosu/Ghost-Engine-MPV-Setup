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
    local rate = bitrate_for(info.formats or {})
    if rate then
        mp.set_property("file-local-options/hls-bitrate", tostring(rate))
    end
    mp.msg.info("live stream: the HLS master playlist, sound and picture together" ..
                (rate and (" (up to " .. rate .. " b/s)") or ""))
    mp.set_property("stream-open-filename", manifest)
end)
