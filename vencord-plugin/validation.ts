/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 TheTakanosu and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * What GhostPlay will let reach a local media player.
 *
 * These checks are the plugin's security boundary: the button is content-based,
 * so anyone's .m3u can arrive here, and the renderer that asked for it cannot
 * be trusted either. They live in their own file for two reasons. A test can
 * import them without pulling in Electron, and native.ts stays what Vencord's
 * PluginNative type requires it to be — a module whose exports are all IPC
 * handlers.
 */

/** Whether a path may be executed as the player.
 *
 *  `mpvPath` arrives from the renderer, and the renderer is inside the sandbox.
 *  Without this check the setting is "run this file for me", and a caller that
 *  never touched the button could name any executable on the machine.
 *
 *  The last segment is taken by hand rather than with `path.basename`, which is
 *  platform-specific: on Linux it would read all of `C:\mpv\mpv.exe` as the
 *  file name. Both separators are split here whatever the platform. */
export function mpvBinaryAllowed(path: string): boolean {
    const name = path.split(/[\\/]/).pop() ?? "";
    return /^mpv(\.exe)?$/i.test(name);
}

/** Discord's own CDN, and nothing else. An attachment URL from anywhere is a
 *  URL an attacker chose, and this one is about to be downloaded and played. */
const ATTACHMENT_HOSTS = new Set([
    "cdn.discordapp.com",
    "media.discordapp.net",
]);

/** Whether one playlist entry may be handed to the player.
 *
 *  Checked by *scheme*, not by character. The first version of this filtered on
 *  a character class and rejected 19 of 28 entries in a real Spotify export:
 *  `ytdl://ytsearch1:MINESTYLE Vyzer, Lytra, wasty` is a perfectly ordinary
 *  track, and commas, brackets, `!` and non-ASCII letters are ordinary in music.
 *
 *  Shell metacharacters are not what makes an entry dangerous here — mpv is
 *  never started through a shell, so they are just text. What is dangerous is
 *  mpv's own protocol handlers: `file://`, `edl://`, `archive://`, `memory://`
 *  and friends read from the local disk. Naming the two shapes that are allowed
 *  excludes all of them without guessing at a blocklist.
 */
export function entryAllowed(line: string): boolean {
    // Control characters have no business in a URL and can confuse a parser.
    if (/[\u0000-\u001f\u007f]/.test(line)) return false;

    // A direct media link.
    if (/^https?:\/\/\S/i.test(line)) return true;

    const rest = /^ytdl:\/\/(.+)$/i.exec(line)?.[1];
    if (!rest) return false;

    // Three shapes and no others: a ytsearch query, which is how a Spotify
    // import resolves at play time; a bare YouTube id; or a URL handed
    // straight to yt-dlp.
    return /^ytsearch\d*:/i.test(rest)
        || /^[A-Za-z0-9_-]{11}$/.test(rest)
        || /^https?:\/\/\S/i.test(rest);
}

export function validatePlaylist(text: string): string | null {
    const lines = text.split(/\r?\n/);
    if (!lines[0]?.startsWith("#EXTM3U")) return "That file is not an M3U playlist.";

    let entries = 0;
    for (const raw of lines) {
        const line = raw.trim();
        if (!line || line.startsWith("#")) continue;
        if (!entryAllowed(line)) {
            return "That playlist points somewhere unexpected, so it was not opened.";
        }
        entries++;
    }
    return entries ? null : "That playlist has no tracks.";
}

/** Whether an attachment URL may be downloaded at all.
 *
 *  Exported so a test can hold this line. The renderer no longer filters by
 *  sender, so "where did this file come from" is decided here and nowhere
 *  else. Membership is exact: `cdn.discordapp.com.evil.test` and
 *  `https://cdn.discordapp.com@evil.test/x` both parse to a hostname that is
 *  not in the set. */
export function attachmentAllowed(url: string): boolean {
    let parsed: URL;
    try {
        parsed = new URL(url);
    } catch {
        return false;
    }
    return parsed.protocol === "https:" && ATTACHMENT_HOSTS.has(parsed.hostname);
}
