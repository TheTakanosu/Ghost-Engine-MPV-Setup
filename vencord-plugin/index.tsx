/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 TheTakanosu and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * GhostPlay — opens a YouTube link or an .m3u playlist posted in Discord in the
 * mpv on your own machine, instead of Discord's embedded player.
 *
 * This half only decides which messages get a button and what to show
 * afterwards. It deliberately makes no security decision: it runs in the
 * sandbox, so any check here can be skipped by code that calls the native
 * handler directly. Everything that matters — what may run, and whether to run
 * it at all — happens in ./native.ts.
 */

import { definePluginSettings } from "@api/Settings";
import definePlugin, { OptionType, PluginNative } from "@utils/types";
import { Message } from "@vencord/discord-types";
import { ChannelStore, showToast, Toasts } from "@webpack/common";

const Native = VencordNative.pluginHelpers.GhostPlay as PluginNative<typeof import("./native")>;

const YOUTUBE_RE =
    /https?:\/\/(?:www\.|m\.)?(?:youtube\.com\/watch\?(?:[^\s]*&)?v=([A-Za-z0-9_-]{11})|youtu\.be\/([A-Za-z0-9_-]{11}))/;

const settings = definePluginSettings({
    mpvPath: {
        type: OptionType.STRING,
        description:
            "Full path to mpv. Leave empty to search the usual places and your PATH. The file must be named mpv or mpv.exe — GhostPlay will not launch anything else.",
        default: "",
    },
    companionScripts: {
        type: OptionType.BOOLEAN,
        description:
            "Let mpv's own scripts run as configured. Turn this off to launch mpv with GHOST_RPC_DISABLE=1, which stops the optional Ghost Engine presence script from starting an agent.",
        default: true,
    },
});

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

            return {
                label: target.kind === "m3u" ? "Play playlist in mpv" : "Play in mpv",
                icon: GhostIcon,
                message,
                channel: ChannelStore.getChannel(message.channel_id),
                onClick: async () => {
                    const result = await Native.play({
                        kind: target.kind,
                        url: target.url,
                        mpvPath: settings.store.mpvPath.trim(),
                        companionScripts: settings.store.companionScripts,
                    });

                    if (result.ok) {
                        showToast(`Playing ${target.label} in mpv`, Toasts.Type.SUCCESS);
                        return;
                    }

                    // The native side asks before it starts anything. Saying no
                    // there is a decision, not a failure, so it gets no toast.
                    if (result.cancelled) return;

                    showToast(result.error, Toasts.Type.FAILURE);
                },
            };
        },
    },
});
