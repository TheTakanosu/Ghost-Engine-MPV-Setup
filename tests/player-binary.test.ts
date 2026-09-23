/*
 * What GhostPlay is willing to execute.
 *
 * `mpvPath` is a plugin setting, which means it lives in the renderer, which
 * means it arrives at the native side as a string a caller chose. Before this
 * check existed, that string went straight to `spawn` once the file existed on
 * disk — so a caller that never touched the button could have started any
 * program on the machine with one argument.
 *
 * The check is deliberately about the file NAME, so these tests are mostly
 * about the ways a name can be dressed up to look like mpv.
 *
 *   node --test tests/
 */
import { strict as assert } from "node:assert";
import { test } from "node:test";

import { mpvBinaryAllowed } from "../vencord-plugin/validation.ts";

test("a real mpv path is accepted on every platform", () => {
    const ACCEPTED = [
        "C:\\mpv\\mpv.exe",
        "C:\\Program Files\\mpv\\mpv.exe",
        "/usr/bin/mpv",
        "/opt/homebrew/bin/mpv",
        "/Applications/mpv.app/Contents/MacOS/mpv",
        "mpv",
        "mpv.exe",
    ];

    for (const path of ACCEPTED) {
        assert.equal(mpvBinaryAllowed(path), true, `${path} was refused`);
    }
});

test("both separators are understood whatever the platform", () => {
    // path.basename would read all of "C:\\Windows\\System32\\calc.exe" as the
    // file name when running on Linux, and let it through. The check splits on
    // both separators itself for exactly this reason.
    assert.equal(mpvBinaryAllowed("C:\\Windows\\System32\\calc.exe"), false);
    assert.equal(mpvBinaryAllowed("C:/Windows/System32/calc.exe"), false);
    assert.equal(mpvBinaryAllowed("C:\\mpv\\mpv.exe"), true);
    assert.equal(mpvBinaryAllowed("C:/mpv/mpv.exe"), true);
});

test("anything that is not mpv is refused", () => {
    const REFUSED: Array<[string, string]> = [
        ["C:\\Windows\\System32\\cmd.exe", "a shell"],
        ["C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", "powershell"],
        ["/bin/sh", "a POSIX shell"],
        ["/usr/bin/curl", "something that downloads"],
        ["C:\\Users\\me\\Downloads\\totally-safe.exe", "an ordinary payload"],
        ["", "an empty path"],
        ["C:\\mpv\\", "a directory, not a file"],
        ["/usr/bin/", "a directory on POSIX"],
    ];

    for (const [path, why] of REFUSED) {
        assert.equal(mpvBinaryAllowed(path), false, `${why}: ${JSON.stringify(path)} was allowed`);
    }
});

test("a name that only looks like mpv is refused", () => {
    const LOOKALIKES = [
        "notmpv.exe",
        "mpv-setup.exe",
        "mpv.exe.bat",
        "mpv.bat",
        "mpv.cmd",
        "mpv.com",
        "mpv.exe.vbs",
        "mpv ",
        " mpv",
        "mpv.exe.",
    ];

    for (const name of LOOKALIKES) {
        assert.equal(mpvBinaryAllowed(name), false, `${JSON.stringify(name)} was allowed`);
    }
});

test("the extension is matched case-insensitively", () => {
    // Windows does not care about case, so neither can this check: refusing
    // MPV.EXE would only break people whose path happens to be capitalised.
    assert.equal(mpvBinaryAllowed("C:\\MPV\\MPV.EXE"), true);
    assert.equal(mpvBinaryAllowed("C:\\mpv\\Mpv.Exe"), true);
    assert.equal(mpvBinaryAllowed("/usr/bin/MPV"), true);
});
