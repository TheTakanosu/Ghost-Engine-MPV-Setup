# Ghost Engine MPV Setup

Watch and listen to YouTube on your own machine instead of in a browser tab —
on the second monitor, in the background, without a browser eating your RAM.

One download, one double-click. It sets up [mpv](https://mpv.io/) with a
configuration that:

- **plays playlists from Discord.** The
  [Takanosu YT-Indexer](https://github.com/TheTakanosu/Takanosu-YT-Indexer) bot
  exports a `.m3u`; open it and the whole list plays.
- **switches itself to audio-only** when the list is an audio export — no flags
  to remember, and you still get a window with the usual seek and volume
  controls.
- **shows what you are playing on Discord**, with the video's own thumbnail, the
  channel, and a real progress bar.

---

## Install

1. Download **`Ghost-Engine-MPV-Setup.zip`** from
   [Releases](https://github.com/TheTakanosu/Ghost-Engine-MPV-Setup/releases/latest)
2. Unpack it anywhere
3. Double-click **`install.bat`**

It asks where to install (`C:\mpv` by default), whether you want node, Rich
Presence and file associations, and then it is done. Takes under a minute on a
normal connection. No administrator rights.

**Windows 10 (1803 or newer) or Windows 11.** That is the version that started
shipping `tar.exe`, which is what unpacks mpv.

Undo it with **`uninstall.bat`**, which deletes the install folder and nothing
else.

---

## What gets installed, and where it comes from

**This download contains no `.exe` files.** Everything executable is fetched
from the project that publishes it, at install time:

| | From | Checked |
|---|---|---|
| `mpv.exe` | [shinchiro's Windows builds](https://github.com/shinchiro/mpv-winbuild-cmake/releases) — the ones mpv.io points at | — |
| `yt-dlp.exe` | [yt-dlp releases](https://github.com/yt-dlp/yt-dlp/releases/latest) | ✅ SHA-256, against the sum yt-dlp publishes with the release |
| `node.exe` *(optional)* | [nodejs.org](https://nodejs.org/dist/latest/win-x64/) | — |

Only the small stuff is ours: `mpv.conf`, three Lua scripts and a Python agent,
all of which you can read in this repo in about five minutes.

This is on purpose. An installer that hands you binaries asks you to trust
whoever built them; one that fetches from the source and verifies the checksum
does not. If yt-dlp's file does not match its published sum, the install stops
and says so rather than carrying on.

**It also means yt-dlp is current.** That matters more than it sounds: YouTube
changes its player often and yt-dlp ships fixes within days. A bundled copy
would be broken within weeks, and it would look like *this* was what broke.

### Staying current

yt-dlp is the piece that breaks when YouTube changes its player, and it is also
the one that is easy to forget about. So mpv checks — **at most once a day, in
the background, never blocking playback** — whether the yt-dlp next to it has
fallen behind, and puts a line on screen if it has:

```
yt-dlp is out of date (2026.08.19 → 2026.09.11)
Videos may stop resolving. Run update.bat in your mpv folder.
```

`update.bat` in the install folder fetches the current yt-dlp under the same
rules as the installer — from yt-dlp's own release, checked against its
published SHA-256, and nothing is replaced if the sum does not match.

**It never updates by itself.** A media player that silently replaces an
executable on your disk because you opened a video is not a trade anyone agreed
to, and on a metered connection it is worse than rude. It tells you; you decide.

Turn the check off with `GHOST_NO_UPDATE_CHECK=1`, or by deleting
`scripts/updatecheck.lua`.

---

## Using it

Drag a `.m3u` onto `mpv.exe`, or just double-click one if you let the installer
register the file types.

An audio export announces itself in the console with `Ghost-Audio is online!`
and plays with no video track but a normal window. A video export plays
normally. Both loop.

Useful keys once it is playing: <kbd>Space</kbd> pause, <kbd>&larr;</kbd>
<kbd>&rarr;</kbd> seek, <kbd>&uarr;</kbd> <kbd>&darr;</kbd> volume,
<kbd>&lt;</kbd> <kbd>&gt;</kbd> previous / next track, <kbd>q</kbd> quit.

### Getting playlists

Add the bot to your Discord server:

```
https://discord.com/oauth2/authorize?client_id=1530546215303909518&permissions=117760&scope=bot
```

Then `!s <search>` to find things, `!add <num>` to queue them, and `!export` or
`!exportmp3` for the `.m3u`. `!commands` lists the rest.

---

## Discord Rich Presence

If you said yes to Rich Presence, there is nothing else to do: mpv starts the
agent when it opens and the agent exits a few seconds after mpv closes. Nothing
is added to Windows' startup, and there is nothing running while you are not
playing anything.

The card shows the track, the channel, the video's thumbnail and a progress bar,
and reports **Listening** for audio and **Watching** for video.

To turn it off for one session, set `GHOST_RPC_DISABLE=1` before starting mpv.
To see what the agent is doing — or why it cannot reach Discord — run
`ghost_rpc.bat` from the install folder; it prints everything to a console.

### cookies.txt

Optional, and **never share it**: it holds live Google session cookies, and
anyone who gets that file can sign into your account. You do not need one for
normal playback — add one only if you hit age-restricted videos or rate
limiting. Drop it next to `mpv.conf` and `scripts/cookies.lua` picks it up.

---

## The Vencord plugin (optional)

**[vencord-plugin/](vencord-plugin/)** adds a play button to the bot's messages
in Discord, so a playlist opens in your mpv without downloading the `.m3u`
first.

It is optional in the strongest sense: it needs a client modification, and the
bot and this setup both work completely without it. Its README covers what it
does, how to build it, and the threat model — it hands things from Discord to a
local process, so who is allowed to trigger that is spelled out rather than
assumed.

---

## If something is wrong

**Videos will not play.** Almost always yt-dlp being out of date against a
YouTube change — run `update.bat` in the install folder. mpv normally warns you
about this on its own before it gets that far. If it still fails, node is worth
installing; YouTube increasingly asks the player to run JavaScript.

**No window on an audio playlist.** You are on an older copy of `ghost.lua`;
re-run `install.bat`.

**Rich Presence not showing.** Run `ghost_rpc.bat` and read what it says. The
usual answers are that Discord is not running, or `pypresence` is not installed
(`py -m pip install pypresence`).

**Nothing above helped:** [Discord](https://discord.gg/w6cR8JU3qP).

---

## Design decisions

Places where the obvious choice was rejected, and why. Most of these were measured against the real client rather than reasoned about.

**Discord fits two RPC buttons on one row only under ~30 characters.** Measured:
`Watch on YouTube` + `Discord Server` (30) sits in a row; adding a `▶` (32) stacks them.
There is no API for asking. `Watch YouTube` would add margin but reads as watching the
site rather than the video.

**The Rich Presence timer anchors to playback, not to the track change.** yt-dlp takes
several seconds to resolve a track, and mpv knows the title before audio starts —
anchoring there counted the whole load and put the clock seconds ahead. The exporter
reports `core-idle`, so the agent shows the track without a timer until sound actually
starts, and re-anchors if the clock drifts more than 3 s.

**mpv starts the Rich Presence agent; Windows does not.** The agent used to be
installed as a Startup-folder shortcut, which meant it ran from sign-in to shut-down to
serve a player that is closed most of the day — and Task Manager's Startup tab labelled
the row `pythonw.exe`, because that tab names the program being launched rather than the
shortcut. `rpc_exporter.lua` now spawns the agent when mpv opens, and the agent exits a
few seconds after mpv is gone. There is no startup entry to recognise or to remove, and
nothing to install.

**The interpreter is not renamed for a prettier Task Manager row.** That was the other
way to fix the label: copying `pythonw.exe` to `GhostEngineRPC.exe` works and does
produce a nicer one — but renaming an interpreter to disguise it is a textbook malware
technique that antivirus software flags. Shipping a compiled `.exe` instead trades a
readable Python file for an unsigned binary a stranger should trust *less*.

**The agent reads the player before it requires Discord.** The reconnect backoff ends in
a `continue`, so checking the connection first meant an agent that could not reach
Discord never got as far as reading the bridge — and would sit in that backoff forever
waiting to report a player that had already closed, which is precisely the leftover
process the design is meant to avoid. The same check covers the other end: an agent that
has not seen a player within 30 seconds of starting leaves too, so there is no state in
which it waits indefinitely for an mpv that is never coming. An advisory lock file,
released by the operating system when its holder dies, keeps a second mpv window from
starting a second agent.

**Audio exports still get a window.** `#GHOST_AUDIO` sets `vid=no`, and with no video
track mpv opens no window at all — leaving a playlist running with no way to seek, pause
or change the volume short of a terminal nobody has open. `ghost.lua` sets `force-window`
alongside it, so an audio list gets the usual window and on-screen controls.

---

## Licence

GPL-3.0, same as the bot. See [LICENSE](LICENSE).
