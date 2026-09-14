/*
 * What may be handed to a local media player.
 *
 * GhostPlay downloads a .m3u posted in Discord and opens it in mpv. An .m3u is
 * a list of things a player will happily open, and mpv's protocol handlers
 * reach the local disk — file://, edl://, archive://, memory://, avdevice://
 * and friends. So the entries are checked before the file is opened, and this
 * is the test that says the check still holds.
 *
 * The rejections matter more than the acceptances. But the acceptances are not
 * decoration either: the first version of this validator filtered on a
 * character class and threw out 19 of 28 entries in a real Spotify export,
 * because commas, brackets, `!` and non-ASCII letters are ordinary in music.
 * Both halves are regressions worth catching.
 *
 * Needs no build step and no dependencies: Node strips the types and runs the
 * source the plugin actually ships.
 *
 *   node --test tests/
 */
import { strict as assert } from "node:assert";
import { test } from "node:test";

import { entryAllowed } from "../vencord-plugin/validation.ts";

/** Things that must never reach the player. */
const HOSTILE: Array<[string, string]> = [
    ["file:///C:/Windows/System32/config/SAM", "file:// reads the local disk"],
    ["C:\\Windows\\System32\\calc.exe", "a Windows path"],
    ["/etc/passwd", "a POSIX path"],
    ["\\\\attacker\\share\\payload.exe", "a UNC share"],
    ["edl://!no_clip_mgmt;C:/secret.txt", "mpv's edl:// can name local files"],
    ["archive://C:/x.zip|file.mp4", "archive:// reaches into local archives"],
    ["memory://whatever", "memory://"],
    ["avdevice://dshow:video=cam", "avdevice:// opens hardware"],
    ["lavf://something", "lavf://"],
    ["fd://0", "fd://"],
    ["ytdl://file:///etc/passwd", "a scheme smuggled behind ytdl://"],
    ["ytdl:///etc/passwd", "an absolute path behind ytdl://"],
    ["ytdl://../../../../etc/passwd", "traversal behind ytdl://"],
    ["javascript:alert(1)", "javascript:"],
    ["data:text/html,<script>", "data:"],
    ["smb://server/share", "smb://"],
    ["ftp://host/file.mp4", "ftp://"],
    ["ytdl://ok\u0000truncated", "an embedded NUL"],
    ["", "an empty line"],
];

/** Things the bot really produces, which must keep working. */
const LEGITIMATE: Array<[string, string]> = [
    ["ytdl://dQw4w9WgXcQ", "a plain YouTube id"],
    ["ytdl://ytsearch1:MINESTYLE Vyzer, Lytra, wasty", "commas in a Spotify export"],
    ["ytdl://ytsearch1:CRAZI FROG (WANT SUM) Vyzer", "brackets"],
    ["ytdl://ytsearch1:ON HER KNEES! Vyzer, wasty, Pröz", "an exclamation mark and non-ASCII"],
    ["ytdl://ytsearch1:Sezen Aksu - Unutmuş mu? (Cover) & Live", "Turkish letters and an ampersand"],
    ["ytdl://ytsearch1:beats to relax/study to 📚", "a slash and an emoji inside a query"],
    ["ytdl://https://www.youtube.com/watch?v=dQw4w9WgXcQ", "a URL handed to yt-dlp"],
    ["https://example.com/stream.mp3", "a direct media link"],
    ["http://example.com/stream.mp3", "plain http"],
];

test("hostile entries are refused", () => {
    for (const [entry, why] of HOSTILE) {
        assert.equal(entryAllowed(entry), false, `${why}: ${JSON.stringify(entry)} was allowed`);
    }
});

test("the entries the bot writes are accepted", () => {
    for (const [entry, why] of LEGITIMATE) {
        assert.equal(entryAllowed(entry), true, `${why}: ${JSON.stringify(entry)} was refused`);
    }
});

test("a shell metacharacter is not by itself a reason to refuse", () => {
    // mpv is never started through a shell, so `&` and `;` in a track title
    // are text. Treating them as dangerous is what threw out most of a real
    // playlist; the scheme is what decides.
    assert.equal(entryAllowed("ytdl://ytsearch1:AC/DC & Friends; live"), true);
    assert.equal(entryAllowed("ytdl://ytsearch1:`backticks` $dollars"), true);
});

test("the scheme is matched case-insensitively", () => {
    assert.equal(entryAllowed("YTDL://dQw4w9WgXcQ"), true);
    assert.equal(entryAllowed("HTTPS://example.com/a.mp3"), true);
    assert.equal(entryAllowed("FILE:///etc/passwd"), false);
});

test("a bare ytdl:// with nothing after it is refused", () => {
    assert.equal(entryAllowed("ytdl://"), false);
});
