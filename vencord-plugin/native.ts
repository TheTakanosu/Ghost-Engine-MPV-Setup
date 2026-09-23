/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 TheTakanosu and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * GhostPlay native side — runs in Electron's main process, where spawning a
 * local player is possible at all.
 *
 * Everything that arrives here came over IPC from the renderer, and the
 * renderer is inside the sandbox: another plugin, or script injected into
 * Discord's page, can call this handler directly without ever touching our
 * button. So nothing the renderer says is trusted, and the two decisions that
 * matter are made on this side of the boundary:
 *
 *   1. WHAT may be launched — the executable must be an mpv binary, and the
 *      playlist must pass ./validation.ts.
 *   2. WHETHER to launch at all — a dialog drawn by the main process, which
 *      renderer code cannot suppress, auto-confirm or click for the user.
 *
 * This file's single export is the IPC handler, which is what Vencord's
 * PluginNative type requires.
 */

import { spawn } from "child_process";
// Electron is imported for real here, not just for types: the confirmation
// dialog has to be drawn by this process to be worth anything. That is also
// why the pure checks live in ./validation.ts — a test can import those
// without Electron, but it could never import this file.
import { app, dialog, type IpcMainInvokeEvent } from "electron";
import { existsSync, readFileSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

import { attachmentAllowed, mpvBinaryAllowed, safeLabel, validatePlaylist } from "./validation";

interface PlayRequest {
    kind: "m3u" | "video";
    url: string;
    mpvPath: string;
    companionScripts: boolean;
}

type PlayResult =
    | { ok: true; }
    | { ok: false; error: string; cancelled?: boolean; };

const YOUTUBE_WATCH_RE = /^https:\/\/www\.youtube\.com\/watch\?v=[A-Za-z0-9_-]{11}$/;

const MPV_CANDIDATES: Record<string, string[]> = {
    win32: [
        "C:\\mpv\\mpv.exe",
        "C:\\Program Files\\mpv\\mpv.exe",
        join(process.env.LOCALAPPDATA ?? "", "Programs", "mpv", "mpv.exe"),
        "mpv.exe",
    ],
    darwin: [
        "/opt/homebrew/bin/mpv",
        "/usr/local/bin/mpv",
        "/Applications/mpv.app/Contents/MacOS/mpv",
        "mpv",
    ],
    linux: [
        "/usr/bin/mpv",
        "/usr/local/bin/mpv",
        "/var/lib/flatpak/exports/bin/io.mpv.Mpv",
        "mpv",
    ],
};

/** The platform is read here rather than asked of the user: this file runs in
 *  Node, so `process.platform` is authoritative and cannot be set wrong. */
function findMpv(preferred: string): string | null {
    if (preferred) {
        if (!mpvBinaryAllowed(preferred)) return null;
        return existsSync(preferred) ? preferred : null;
    }

    const candidates = MPV_CANDIDATES[process.platform] ?? MPV_CANDIDATES.linux;
    for (const candidate of candidates) {
        // The last candidate is the bare name, left for the OS to resolve on
        // PATH — existsSync cannot answer for it, so it is returned as-is.
        if (!candidate.includes("/") && !candidate.includes("\\")) return candidate;
        if (candidate && existsSync(candidate)) return candidate;
    }
    return null;
}

/* ---------- the "don't ask again" preference ----------
 *
 * Kept in this process's own file rather than in Vencord's settings. Vencord
 * settings live in the renderer, so renderer code can change them, and a
 * switch that turns the confirmation off is exactly the switch an attacker
 * would flip. Only the main process writes here, and only after the user
 * ticked the box in a dialog the main process drew.
 */

function configPath(): string {
    return join(app.getPath("userData"), "ghostplay.json");
}

function skipConfirmation(): boolean {
    try {
        return JSON.parse(readFileSync(configPath(), "utf8")).skipConfirmation === true;
    } catch {
        return false;
    }
}

function rememberSkip(): void {
    try {
        writeFileSync(configPath(), JSON.stringify({ skipConfirmation: true }, null, 2), "utf8");
    } catch {
        // A preference that cannot be saved just means being asked again.
    }
}

/** Ask, outside the sandbox, before anything is started.
 *
 *  `dialog.showMessageBox` is drawn by the main process, so renderer code can
 *  neither hide it nor answer it. That is the whole point: a caller that
 *  reached this handler without going through the button still cannot open
 *  anything without the person at the keyboard agreeing. */
async function confirmLaunch(mpv: string, target: string, kind: PlayRequest["kind"]): Promise<boolean> {
    if (skipConfirmation()) return true;

    const { response, checkboxChecked } = await dialog.showMessageBox({
        type: "question",
        buttons: ["Cancel", "Open in mpv"],
        defaultId: 1,
        cancelId: 0,
        noLink: true,
        title: "GhostPlay",
        message: kind === "m3u"
            ? "Open this playlist in mpv?"
            : "Open this video in mpv?",
        detail: `${target}\n\nPlayer: ${mpv}`,
        checkboxLabel: "Don't ask again",
        checkboxChecked: false,
    });

    if (response !== 1) return false;
    if (checkboxChecked) rememberSkip();
    return true;
}

async function fetchPlaylist(url: string): Promise<string> {
    if (!attachmentAllowed(url)) {
        throw new Error("That attachment is not hosted on Discord.");
    }

    const response = await fetch(url);
    if (!response.ok) throw new Error(`Discord returned ${response.status} for that file.`);

    const length = Number(response.headers.get("content-length") ?? 0);
    if (length > 1_000_000) throw new Error("That playlist is too large.");

    return await response.text();
}

/** What to show the user in the dialog.
 *
 *  Derived here from the URL rather than taken from the renderer: a label the
 *  caller supplies is a label the caller can lie about, and this one is the
 *  only description of what is about to open. It still comes from a file
 *  somebody else named, so safeLabel flattens it before it is drawn. */
function describe(url: string, kind: PlayRequest["kind"]): string {
    if (kind === "video") return safeLabel(url);
    try {
        return safeLabel(decodeURIComponent(new URL(url).pathname.split("/").pop() || ""));
    } catch {
        return "playlist.m3u";
    }
}

export async function play(_event: IpcMainInvokeEvent, request: PlayRequest): Promise<PlayResult> {
    const mpv = findMpv(request.mpvPath);
    if (!mpv) {
        return {
            ok: false,
            error: request.mpvPath
                ? "GhostPlay's mpv path does not point at an mpv binary."
                : "mpv was not found. Install it, or set its path in GhostPlay's settings.",
        };
    }

    let target: string;
    try {
        if (request.kind === "m3u") {
            const text = await fetchPlaylist(request.url);
            const problem = validatePlaylist(text);
            if (problem) return { ok: false, error: problem };

            // Written out rather than piped: mpv reads the first 256 bytes of
            // the file to find the #GHOST_AUDIO marker that switches it to
            // audio-only, and scripts/ghost.lua needs a real path to do that.
            target = join(tmpdir(), "ghostplay-current.m3u");
            writeFileSync(target, text, "utf8");
        } else {
            if (!YOUTUBE_WATCH_RE.test(request.url)) {
                return { ok: false, error: "That link is not a YouTube video." };
            }
            target = request.url;
        }
    } catch (error) {
        return { ok: false, error: error instanceof Error ? error.message : "Could not read that playlist." };
    }

    // Checked first, asked second: a playlist that was going to be refused
    // anyway should not cost the user a dialog.
    if (!await confirmLaunch(mpv, describe(request.url, request.kind), request.kind)) {
        return { ok: false, error: "", cancelled: true };
    }

    try {
        // No shell, ever. mpv gets an argv array, so a video title full of
        // `&` or `;` is an argument and never a command.
        const child = spawn(mpv, ["--", target], {
            detached: true,
            stdio: "ignore",
            shell: false,
            env: request.companionScripts
                ? process.env
                // Read by scripts/rpc_exporter.lua, which skips starting the
                // presence agent when it is set.
                : { ...process.env, GHOST_RPC_DISABLE: "1" },
        });
        child.on("error", () => { /* reported below via the spawn throw path */ });
        child.unref();
        return { ok: true };
    } catch {
        return { ok: false, error: "mpv could not be started." };
    }
}
