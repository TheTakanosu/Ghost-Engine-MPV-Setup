-- ==========================================
-- GHOST ENGINE - PLAY DOWNLOADED SONGS FROM DISK
-- ==========================================
-- The Ghost Engine app can download a playlist for offline listening, the
-- way Spotify does. It keeps the songs in
--   %LOCALAPPDATA%\TheTakanosu\Ghost Engine\offline
-- with an index.tsv beside them: one line per song, the playlist entry as
-- the .m3u has it, a tab, and the file it was saved as.
--
-- When an entry that has a file starts, mpv is pointed at the file instead
-- of YouTube. It opens at once, works with no internet, and skips the
-- search and extraction that make a song take seconds to start. This runs
-- before cookies.lua (5), songmatch.lua (8) and mpv's own ytdl hook (10);
-- all three leave a local file alone.
--
-- Nothing downloaded, or no app installed: the index is not there and
-- every song plays from YouTube as before. Delete this file to turn it off.

local utils = require "mp.utils"

local base = os.getenv("LOCALAPPDATA")
if not base then return end
local folder = base .. "\\TheTakanosu\\Ghost Engine\\offline"
local index_path = folder .. "\\index.tsv"

-- Read again only when the app has written it since.
local files, read_size, read_time = {}, -1, -1
local playing_id = nil   -- the video of the file being opened, if it is ours

local function load_index()
    local info = utils.file_info(index_path)
    if not info then
        files, read_size, read_time = {}, -1, -1
        return
    end
    if info.size == read_size and info.mtime == read_time then return end
    files, read_size, read_time = {}, info.size, info.mtime
    local f = io.open(index_path, "rb")
    if not f then return end
    for line in f:lines() do
        local target, name = line:match("^(.-)\t([^\t\r]+)\r?$")
        if target and name then files[target] = name end
    end
    f:close()
end

mp.add_hook("on_load", 4, function()
    -- A downloaded copy is only the sound: a song list switched to videos
    -- (ghost.lua) plays each song's video from YouTube instead.
    if mp.get_property_native("user-data/ghost/video-mode") == true then return end
    load_index()
    local entry = mp.get_property("path", "")
    local name = files[entry]
    if not name then return end
    local file = folder .. "\\" .. name
    if not utils.file_info(file) then return end
    mp.set_property("stream-open-filename", file)
    -- The file has no tags; the title the list gives the song shows instead
    -- of the file name, for this file only.
    local title = mp.get_property("playlist/" .. mp.get_property("playlist-pos", "0") .. "/title", ""):match("^%s*(.-)%s*$")
    if title ~= "" then mp.set_property("file-local-options/force-media-title", title) end
    mp.msg.info("playing the downloaded copy: " .. name)
    -- The file is named after its video; that is said once the file has
    -- loaded (songmatch.lua clears it on every load), so the app and Rich
    -- Presence still find the song's cover.
    playing_id = name:match("^([%w_-]+)%.")
end)

mp.register_event("file-loaded", function()
    if playing_id then
        mp.set_property_native("user-data/ghost/source", "https://www.youtube.com/watch?v=" .. playing_id)
        playing_id = nil
    end
end)
