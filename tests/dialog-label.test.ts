/*
 * What the confirmation dialog is allowed to say.
 *
 * The dialog is the last thing between a Discord message and a process on the
 * machine, so what it shows has to be trustworthy. The file name in it comes
 * from an attachment somebody else uploaded and is URL-decoded on the way, so
 * `%0A` in the upload name arrives as a real line break — enough to draw extra
 * lines that read like the dialog's own text.
 *
 *   node --test tests/
 */
import { strict as assert } from "node:assert";
import { test } from "node:test";

import { safeLabel } from "../vencord-plugin/validation.ts";

test("ordinary names are left alone", () => {
    assert.equal(safeLabel("queue.m3u"), "queue.m3u");
    assert.equal(safeLabel("Sezen Aksu - Unutmuş mu (Live).m3u"), "Sezen Aksu - Unutmuş mu (Live).m3u");
    assert.equal(safeLabel("https://www.youtube.com/watch?v=dQw4w9WgXcQ"), "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
});

test("a name cannot draw its own lines in the dialog", () => {
    const forged = "song.m3u\n\nPlayer: mpv\nThis file is safe to open";
    const shown = safeLabel(forged);

    assert.ok(!shown.includes("\n"), "a line break survived into the dialog");
    assert.ok(!shown.includes("\r"), "a carriage return survived into the dialog");
    // Each control character becomes one space, so the two blank-line newlines
    // collapse to two spaces rather than disappearing — the forged text is still
    // there to read, it just cannot pose as a separate line.
    assert.equal(shown, "song.m3u  Player: mpv This file is safe to open");
});

test("control characters are removed, not just newlines", () => {
    for (const ch of ["\u0000", "\u0007", "\u001b", "\u007f", "\u0085", "\u009b"]) {
        const shown = safeLabel(`a${ch}b`);
        assert.ok(!shown.includes(ch), `${JSON.stringify(ch)} survived`);
    }
});

test("a very long name is cut, not allowed to fill the dialog", () => {
    const shown = safeLabel("x".repeat(500));
    assert.equal(shown.length, 120);
    assert.ok(shown.endsWith("..."));
});

test("a name that is only whitespace or control characters still says something", () => {
    assert.equal(safeLabel(""), "(unnamed)");
    assert.equal(safeLabel("   "), "(unnamed)");
    assert.equal(safeLabel("\n\n\t"), "(unnamed)");
});
