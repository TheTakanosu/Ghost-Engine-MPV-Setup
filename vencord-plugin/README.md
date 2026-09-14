# GhostPlay — Vencord plugin

Adds one button to a YouTube link or an `.m3u` playlist posted in Discord: click
it and the track, or the whole list, opens in the mpv on your own machine. No
download, no double-click, no browser tab.

It was written for [Takanosu YT-Indexer](https://github.com/TheTakanosu/Takanosu-YT-Indexer)
exports and it still handles them, but it is **not tied to that bot** — the
button follows the content, so any `.m3u` and any YouTube link works.

**Strictly optional.** The bot, the exports and Rich Presence all work without
it. Nothing in the bot imports anything from this folder — see *No client mod is
required for anything* in the [main README](../README.md).

---

## What the button appears on

| Message | Button | What mpv gets |
|---|---|---|
| any `.m3u` attachment | ✅ | the attached playlist |
| any YouTube link (`youtube.com/watch`, `youtu.be`) | ✅ | that video |
| a message with neither | ❌ | nothing — there is nothing to play |

From the bot specifically, that means `!export` / `!exportmp3` / `!spotify` /
`!spotifymp3` get the playlist button, and `!p` / `!v` / `!play` / `!pn` plus the
**1-9** grid buttons get the video one. `!playlist` and `!show` do not: they
print titles, not links. That is deliberate rather than missing — `!show` answers
*what is in this list*, `!export <name>` answers *give it to me*, and the button
belongs on the answer that contains something to play.

---

## Security

This plugin takes something out of a Discord message and hands it to a process on
your computer. That is worth being careful about, so the design starts from the
assumption that **every message is hostile** and narrows from there.

**The checks are the boundary — there is no trusted sender behind them.** An
earlier version rendered the button only for one allowlisted bot id. That made
the sender the security boundary, which is a weak place to put one: it is a
setting, it is a string, and it cannot be tested. Now the button appears on any
message with something playable, and everything below runs on every file no
matter who posted it.

**The file must come from Discord.** An attachment URL is a URL somebody else
chose, and it is about to be downloaded. It must be HTTPS on `cdn.discordapp.com`
or `media.discordapp.net`, matched as an exact hostname — `cdn.discordapp.com.evil.test`
and `https://cdn.discordapp.com@evil.test/x` both fail.

**The playlist is read before it is played.** An `.m3u` is a list of things a
player will open, so the file is fetched, parsed and checked first:

- under 1 MB, and it must start with `#EXTM3U`
- **every** entry must be `ytdl://<id>`, `ytdl://ytsearch…`, `ytdl://http(s)://…`
  or a plain `http(s)://` link
- one bad entry rejects the whole file, so a playlist that opens with real tracks
  gains nothing

The check is by **scheme**, not by character. That excludes mpv's own local-disk
protocols — `file://`, `edl://`, `archive://`, `memory://`, `avdevice://`,
`fd://` — without guessing at a blocklist. Shell metacharacters are *not*
filtered, because mpv is never started through a shell and `AC/DC & Friends` is
an ordinary track name. Filtering them is what an earlier version did, and it
threw out 19 of 28 entries in a real Spotify export.

**You are asked before a stranger's playlist opens.** A YouTube link is visible
in the message and opens one known video, so the click is consent enough. A
playlist is a file whose contents are not on screen, so opening one shows who
posted it and what it is called first. Put an id in `trustedSenders` — your own
bot, for instance — and its playlists open straight away.

**mpv is never started through a shell.** `spawn(mpv, ["--", target], { shell: false })`
passes an argv array, so a video title full of `&`, `;` or backticks is an
argument and can never become a command. The `--` guards against a target that
starts with a dash being read as an option.

**The native half is as small as it can be.** Only `native.ts` runs with Node
privileges, and its entire surface is one function taking a URL and a couple of
settings. Everything else — deciding which messages qualify, reading settings,
drawing the button — stays in the renderer, where it cannot spawn anything.

**All of it is tested.** [`tests/`](../tests) runs on every push: hostile entries,
look-alike CDN hosts, malformed playlists, and the real exports that must keep
working. No build step and no dependencies — Node strips the types and runs the
source the plugin actually ships.

```bash
node --test tests/*.test.ts
```

---

## Settings

| Setting | Default | What it does |
|---|---|---|
| `trustedSenders` | *(empty)* | Comma-separated ids whose playlists open without asking. Empty means always ask |
| `mpvPath` | *(empty)* | Full path to mpv. Empty means search the usual places, then `PATH` |
| `companionScripts` | on | Whether mpv's own scripts may run as configured |

`companionScripts` works through an environment variable rather than a flag: with
it off, mpv is launched with `GHOST_RPC_DISABLE=1`, and
[`scripts/rpc_exporter.lua`](../mpv/scripts/rpc_exporter.lua) reads that and skips
starting the presence agent. The bridge file is still written, so an agent you
started yourself keeps working. Setting the variable by hand does the same thing.

`trustedSenders` is a convenience, not a security control — it decides whether you
are asked, never what is allowed. Everything in *Security* above runs either way.

**Your operating system is detected, not configured.** `native.ts` runs in Node,
so `process.platform` is authoritative — there is no "pick your OS" setting to get
wrong. Windows, macOS and Linux each have their own list of places to look for mpv
before falling back to `PATH`.

---

## Installing

Vencord has no runtime plugin installation: plugins are compiled in, so a custom
one means building Vencord from source. This is Vencord's own documented workflow
for user plugins, and it is an advanced-user path — their words.

```bash
git clone https://github.com/Vendicated/Vencord
cd Vencord
pnpm i
mkdir -p src/userplugins
cp -r <path-to>/Ghost-Engine-MPV-Setup/vencord-plugin src/userplugins/ghostPlay
pnpm build
pnpm inject          # pick your Discord install; asks before changing anything
```

Then restart Discord and enable **GhostPlay** in Vencord's plugin list.

> Build into **Discord Canary** if you already run Vencord on your normal
> Discord. The two installs are independent, so a dev build cannot disturb the
> one you use every day.

`pnpm uninject` undoes it. You also need [mpv](https://mpv.io/) with `yt-dlp` next
to it — run `install.bat` from this repo's release, which sets that up for you.

On Windows, [`sync-and-build.ps1`](sync-and-build.ps1) copies the plugin into a
checkout and rebuilds in one step.

### Why it is not on a plugin list yet

Building from source is a wall for ordinary users, so the plugin lists are worth
getting onto — but both of the obvious ones have a rule in the way.

[Equicord](https://equicord.org/) takes most submitted plugins into its main
build, which would mean no compiling at all. Its rules reject *"plugins that
interact with specific third-party Discord bots"*. The version above is written
to be judged on its own: no default bot id, nothing that names one bot, and a
feature — play a YouTube link in mpv instead of the embedded player — that is
useful to anyone who uses mpv.

[BetterDiscord](https://betterdiscord.app/) installs plugins by dropping a `.js`
into a folder, with no bot rule at all, but new submissions *"must not make use of
the `child_process` node module"* — and starting mpv is the entire point. Handing
the playlist to a local helper process instead of spawning one would satisfy that,
at the cost of a background listener; it has not been written.
