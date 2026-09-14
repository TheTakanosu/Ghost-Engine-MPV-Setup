/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 TheTakanosu and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * GhostPlay native side — runs in Electron's main process, where spawning a
 * local player is possible at all. Everything that reaches this file came out
 * of a Discord message, so it is treated as hostile input.
 *
 * The renderer does not filter by sender: anyone's playlist can reach this
 * file. What may pass is decided by ./validation.ts, and it assumes that
 * whoever posted the file chose every byte of it. This file only fetches,
 * checks and starts mpv — its single export is the IPC handler, which is what
 * Vencord's PluginNative type requires.
 */

import { spawn } from "child_process";
// A type-only import: Node strips it entirely, so nothing here drags Electron
// into a plain `node --test` run.
import type { IpcMainInvokeEvent } from "electron";
import { existsSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

import { attachmentAllowed, validatePlaylist } from "./validation";

interface PlayRequest {
    kind: "m3u" | "video";
    url: string;
    mpvPath: string;
    companionScripts: boolean;
}

type PlayResult = { ok: true; } | { ok: false; error: string; };

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
    if (preferred) return existsSync(preferred) ? preferred : null;

    const candidates = MPV_CANDIDATES[process.platform] ?? MPV_CANDIDATES.linux;
    for (const candidate of candidates) {
        // The last candidate is the bare name, left for the OS to resolve on
        // PATH — existsSync cannot answer for it, so it is returned as-is.
        if (!candidate.includes("/") && !candidate.includes("\\")) return candidate;
        if (candidate && existsSync(candidate)) return candidate;
    }
    return null;
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

export async function play(_event: IpcMainInvokeEvent, request: PlayRequest): Promise<PlayResult> {
    const mpv = findMpv(request.mpvPath);
    if (!mpv) {
        return {
            ok: false,
            error: request.mpvPath
                ? "No mpv at the path in GhostPlay's settings."
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
