/*
 * Where a playlist may come from, and what a playlist file may contain.
 *
 * GhostPlay used to render its button only on messages from an allowlisted
 * bot, so these two checks stood behind a filter that had already thrown out
 * every other sender. The button is now content-based — anyone's .m3u can
 * reach the native side — which promotes both of them from a second line of
 * defence to the whole of it. That is the reason this file exists.
 *
 *   node --test tests/
 */
import { strict as assert } from "node:assert";
import { test } from "node:test";

import { attachmentAllowed, validatePlaylist } from "../vencord-plugin/validation.ts";

test("only Discord's own CDN may be downloaded from", () => {
    for (const url of [
        "https://cdn.discordapp.com/attachments/1/2/queue.m3u",
        "https://media.discordapp.net/attachments/1/2/queue.m3u",
    ]) {
        assert.equal(attachmentAllowed(url), true, `${url} was refused`);
    }
});

test("a URL that only looks like Discord's CDN is refused", () => {
    const HOSTILE = [
        ["http://cdn.discordapp.com/x.m3u", "plain http"],
        ["https://cdn.discordapp.com.evil.test/x.m3u", "a suffixed look-alike host"],
        ["https://cdn-discordapp.com/x.m3u", "a hyphenated look-alike host"],
        ["https://evil.test/cdn.discordapp.com/x.m3u", "the name moved into the path"],
        ["https://cdn.discordapp.com@evil.test/x.m3u", "the name moved into userinfo"],
        ["https://attacker.test/x.m3u", "somewhere else entirely"],
        ["file:///C:/Windows/System32/drivers/etc/hosts", "a local file"],
        ["not a url at all", "unparseable text"],
        ["", "an empty string"],
    ];

    for (const [url, why] of HOSTILE) {
        assert.equal(attachmentAllowed(url), false, `${why}: ${JSON.stringify(url)} was allowed`);
    }
});

test("the file must actually be an M3U playlist", () => {
    assert.equal(
        validatePlaylist("<!doctype html><html>nope</html>"),
        "That file is not an M3U playlist."
    );
    assert.equal(validatePlaylist(""), "That file is not an M3U playlist.");
});

test("a playlist with no tracks is refused", () => {
    assert.equal(validatePlaylist("#EXTM3U\n#GHOST_AUDIO\n"), "That playlist has no tracks.");
});

test("one hostile entry condemns the whole file", () => {
    // The point of checking every line rather than the first: a playlist that
    // opens with real tracks is exactly how a bad one would be dressed up.
    const playlist = [
        "#EXTM3U",
        "#EXTINF:-1, Real Track",
        "ytdl://dQw4w9WgXcQ",
        "#EXTINF:-1, Also Real",
        "ytdl://ytsearch1:Sezen Aksu - Unutmuş mu?",
        "#EXTINF:-1, Not A Track",
        "file:///C:/Users/me/.ssh/id_ed25519",
    ].join("\n");

    assert.equal(
        validatePlaylist(playlist),
        "That playlist points somewhere unexpected, so it was not opened."
    );
});

test("what the bot really exports is accepted", () => {
    const audio = ["#EXTM3U", "#GHOST_AUDIO", "#EXTINF:-1, Some Track", "ytdl://dQw4w9WgXcQ", ""].join("\n");
    assert.equal(validatePlaylist(audio), null);

    const video = ["#EXTM3U", "#EXTINF:-1, Some Track", "ytdl://ytsearch1:AC/DC & Friends; live"].join("\n");
    assert.equal(validatePlaylist(video), null);
});

test("CRLF line endings are read the same way", () => {
    // The exports are written on a Linux server and opened on Windows.
    const playlist = "#EXTM3U\r\n#EXTINF:-1, Track\r\nytdl://dQw4w9WgXcQ\r\n";
    assert.equal(validatePlaylist(playlist), null);

    const hostile = "#EXTM3U\r\nfile:///etc/passwd\r\n";
    assert.equal(
        validatePlaylist(hostile),
        "That playlist points somewhere unexpected, so it was not opened."
    );
});
