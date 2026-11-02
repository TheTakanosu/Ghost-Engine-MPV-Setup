# Privacy

Ghost Engine has no server and no account. It collects nothing about you and
sends nothing to us: there is no analytics, no telemetry and no crash
reporting. What it knows stays on your PC.

This page covers the app, the player (mpv and its scripts) and Ghost RPC. The
[Takanosu YT-Indexer](https://github.com/TheTakanosu/Takanosu-YT-Indexer)
Discord bot is a separate project.

## What stays on your PC

| What | Where |
|---|---|
| Your playlists | `Music\Ghost Engine` (and any folder you add) |
| Songs you download | `%LOCALAPPDATA%\TheTakanosu\Ghost Engine\offline` |
| Covers, lyrics and song details, kept so they load fast | `%LOCALAPPDATA%\TheTakanosu\Ghost Engine\cache` |
| What you have listened to (play counts, and your history: the last 1,000 plays of the last 90 days) | `%LOCALAPPDATA%\TheTakanosu\Ghost Engine\listening.json` |
| Your Discord name and avatar, for the profile card | `%LOCALAPPDATA%\TheTakanosu\Ghost Engine` |
| Settings | the registry, `HKEY_CURRENT_USER\Software\TheTakanosu\Ghost Engine` |
| Player settings and `cookies.txt`, if you add one | the mpv folder (`C:\mpv` by default) |
| A crash report, if the app ever crashes | `%TEMP%\GhostEngine-crash.txt`; it is never sent anywhere |

The history can be turned off or cleared in Settings > Privacy; turning it
off deletes it. Deleting these removes everything Ghost Engine knows.
`uninstall.bat` removes the mpv folder and the shortcuts only; the rest stays
until you delete it, so your playlists and downloads survive a reinstall.

## Who Ghost Engine talks to

Only these, and only for what is listed:

- **YouTube** (`youtube.com`, `music.youtube.com`, `i.ytimg.com`): to play
  songs and videos, find a song's best match, show covers and an artist's
  details. Playback goes through [yt-dlp](https://github.com/yt-dlp/yt-dlp),
  which asks YouTube the way a browser would.
- **LRCLIB** (`lrclib.net`): the song's title and artist, to find its lyrics.
- **GitHub** (`api.github.com`, `github.com`): to see whether Ghost Engine,
  yt-dlp or mpv has a newer version, and to download it when you say so.
- **Discord** (`cdn.discordapp.com`): your avatar, for the profile card.
- **nodejs.org**: only when the installer or an update fetches node.

None of them gets anything that identifies you beyond what any app's
connection does (your IP address). Opening "Search on Genius" or a link in
the app opens your browser.

## cookies.txt and browser cookies

If you add a `cookies.txt`, or turn on "Use my browser's cookies instead",
your YouTube sign-in is used only to ask YouTube for a song when it wants a
signed-in user. It goes to YouTube and nowhere else, is never copied out of
your PC, and the browser option is off until you turn it on. Treat
`cookies.txt` like a password: do not share it. [How to get one](docs/cookies.md).

## Discord (Ghost RPC)

When it is on, Ghost RPC tells the Discord app on your PC what is playing:
the title, the channel, the playlist's name and the cover. Discord shows that
on your profile, so **your friends and servers see it**. It talks only to
the Discord app on your PC. You can turn songs and videos off separately in
Settings > Discord.

## Changes

If this changes, the change is in this file's history on GitHub.
