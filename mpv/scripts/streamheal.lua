-- ==========================================
-- GHOST ENGINE - PICTURE STREAM HEALER
-- ==========================================
-- YouTube sends a video's picture and its sound as two streams. Now and then
-- the picture's connection ends early and mpv takes that for the end of the
-- video: the sound plays on and the picture stays on its last frame (seen on
-- 2026-11-07: the picture "ended" at 18:06 of a 32:21 video, the sound was
-- downloaded to the end).
--
-- When playback passes the point where the picture's stream stopped, long
-- before the video's end, the buffers are dropped and the same moment is
-- sought: the picture's stream is asked for again and the video goes on
-- from there. At most once every 20 seconds, so a stream that keeps failing
-- does not keep the player seeking.

local mp = require 'mp'
local msg = require 'mp.msg'

local last_heal = -100

local function check()
    if mp.get_property_bool('pause') or mp.get_property_bool('seeking') then
        return
    end
    local vid = mp.get_property('vid')
    if not vid or vid == 'no' then
        return   -- a song as sound only
    end
    local path = mp.get_property('path', '')
    if not (path:find('^https?://') or path:find('^ytdl://')) then
        return   -- a file on disk does not lose its connection
    end
    local duration = mp.get_property_number('duration')
    local pos = mp.get_property_number('time-pos')
    local state = mp.get_property_native('demuxer-cache-state')
    if not duration or not pos or not state or not state['eof'] then
        return
    end
    for _, stream in ipairs(state['ts-per-stream'] or {}) do
        local stop = stream['cache-end']
        if stream['type'] == 'video' and stop and stop < duration - 3 and pos > stop + 1 then
            local now = mp.get_time()
            if now - last_heal < 20 then
                return
            end
            last_heal = now
            msg.warn(string.format('picture stream stopped at %.1f of %.1f, playing at %.1f: asking for it again',
                                   stop, duration, pos))
            mp.commandv('drop-buffers')
            mp.commandv('seek', string.format('%.3f', pos), 'absolute+exact')
            return
        end
    end
end

mp.add_periodic_timer(2, check)
