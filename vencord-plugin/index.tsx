/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 TheTakanosu and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * GhostPlay — opens a YouTube link or an .m3u playlist posted in Discord in the
 * mpv on your own machine, instead of Discord's embedded player.
 */

import { definePluginSettings } from "@api/Settings";
import definePlugin, { OptionType, PluginNative } from "@utils/types";
import { Message } from "@vencord/discord-types";
import { Alerts, ChannelStore, showToast, Toasts } from "@webpack/common";

const Native = VencordNative.pluginHelpers.GhostPlay as PluginNative<typeof import("./native")>;

const YOUTUBE_RE =
    /https?:\/\/(?:www\.|m\.)?(?:youtube\.com\/watch\?(?:[^\s]*&)?v=([A-Za-z0-9_-]{11})|youtu\.be\/([A-Za-z0-9_-]{11}))/;

const settings = definePluginSettings({
    trustedSenders: {
        type: OptionType.STRING,
        description:
            "Comma-separated user IDs whose playlists open without asking first. Everyone else gets a confirmation. Empty means always ask.",
        default: "",
    },
    mpvPath: {
        type: OptionType.STRING,
        description:
            "Full path to mpv. Leave empty to search the usual places and your PATH.",
        default: "",
    },
    companionScripts: {
        type: OptionType.BOOLEAN,
        description:
            "Let mpv's own scripts run as configured. Turn this off to launch mpv with GHOST_RPC_DISABLE=1, which stops the optional Ghost Engine presence script from starting an agent.",
        default: true,
    },
});

/** Senders whose playlists skip the confirmation.
 *
 *  A convenience list, not the security boundary — native.ts checks every entry
 *  of every playlist no matter who posted it. Being absent from this list costs
 *  one extra click, not safety.
 */
function trustedSenders(): Set<string> {
    return new Set(
        settings.store.trustedSenders
            .split(",")
            .map(id => id.trim())
            .filter(id => /^\d{5,25}$/.test(id))
    );
}

/** What, if anything, this message can hand to mpv. */
function playableTarget(message: Message) {
    const m3u = message.attachments?.find(a =>
        a.filename?.toLowerCase().endsWith(".m3u")
    );
    if (m3u) return { kind: "m3u" as const, url: m3u.url, label: m3u.filename };

    const match = YOUTUBE_RE.exec(message.content ?? "");
    if (match) {
        const id = match[1] ?? match[2];
        return { kind: "video" as const, url: `https://www.youtube.com/watch?v=${id}`, label: id };
    }

    return null;
}

function GhostIcon(props: any) {
    return (
        <svg viewBox="0 0 24 24" width={24} height={24} aria-hidden {...props}>
            <path
                fill="currentColor"
                d="M12 2a8 8 0 0 0-8 8v10.3c0 1.1 1.2 1.7 2.1 1.2l1.6-1a1 1 0 0 1 1 0l1.8 1a1 1 0 0 0 1 0l1.8-1a1 1 0 0 1 1 0l1.6 1c.9.5 2.1-.1 2.1-1.2V10a8 8 0 0 0-8-8Z"
            />
            <path fill="var(--background-primary, #000)" d="M10 8.6v5.2l4.4-2.6L10 8.6Z" />
        </svg>
    );
}

export default definePlugin({
    name: "GhostPlay",
    description:
        "Adds a button to YouTube links and .m3u playlists in Discord that opens them in your local mpv.",
    authors: [{ name: "TheTakanosu", id: 0n }],
    settings,

    messagePopoverButton: {
        icon: GhostIcon,
        render(message: Message) {
            const target = playableTarget(message);
            if (!target) return null;

            const open = async () => {
                const result = await Native.play({
                    kind: target.kind,
                    url: target.url,
                    mpvPath: settings.store.mpvPath.trim(),
                    companionScripts: settings.store.companionScripts,
                });

                if (result.ok) {
                    showToast(`Playing ${target.label} in mpv`, Toasts.Type.SUCCESS);
                } else {
                    showToast(result.error, Toasts.Type.FAILURE);
                }
            };

            return {
                label: target.kind === "m3u" ? "Play playlist in mpv" : "Play in mpv",
                icon: GhostIcon,
                message,
                channel: ChannelStore.getChannel(message.channel_id),
                onClick: () => {
                    // A YouTube link is visible in the message and opens one
                    // known video, so clicking the button is consent enough. A
                    // playlist is a file: its contents are not on screen, and
                    // anyone can attach one. Say who it came from before it
                    // opens, unless the reader has already trusted that sender.
                    if (target.kind === "video" || trustedSenders().has(message.author?.id)) {
                        void open();
                        return;
                    }

                    Alerts.show({
                        title: "Open this playlist in mpv?",
                        body: `${target.label} — posted by ${message.author?.username ?? "someone"}. Every entry is checked before mpv starts, but the tracks are whatever that person put in the file.`,
                        confirmText: "Open in mpv",
                        cancelText: "Cancel",
                        onConfirm: () => void open(),
                    });
                },
            };
        },
    },
});
