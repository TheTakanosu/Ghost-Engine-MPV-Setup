# Signing in to YouTube: cookies

Most songs play without any of this. You need it when songs keep failing
and Ghost Engine says YouTube is checking for bots, or for an age-restricted
song. Ghost Engine never signs in for you and never goes looking for your
cookies: you choose one of the two ways below, and you can undo it at any time.

**Whichever you pick, a separate Google account is the safe choice.** YouTube
can restrict an account it takes for a bot, and you would rather that were not
the one with your mail on it.

## The easy way: let yt-dlp read your browser

1. Sign in to YouTube in your browser (the separate account, ideally).
2. In Ghost Engine, open **Settings → YouTube sign-in**.
3. Pick your browser and turn on **Use my browser's cookies instead**, then
   confirm.

Nothing is copied anywhere. When a song fails, yt-dlp reads the sign-in from
the browser's own files for that one retry, the same way the browser sends it
to YouTube. The cookies are always the current ones, so they never expire on
you.

- **Firefox, Zen, LibreWolf, Waterfox and Floorp** work.
- **Chrome, Edge, Brave and Vivaldi** are listed as *may not work*: since
  Chrome 127 they encrypt cookies in a way yt-dlp often cannot read. If songs
  still fail with one of them, use `cookies.txt` instead.

Turn the switch off and Ghost Engine stops asking the browser straight away.

## The manual way: cookies.txt

A `cookies.txt` is a plain text file with your YouTube sign-in in it. Treat it
like a password: **never send it to anyone**, not even to us when asking for
help.

1. Install a cookie exporter that runs on your computer only, for example
   **Get cookies.txt LOCALLY** (open source) for
   [Firefox](https://addons.mozilla.org/firefox/addon/get-cookies-txt-locally/)
   or [Chrome](https://chromewebstore.google.com/detail/get-cookiestxt-locally/cclelndahbckbenkjhflpdbgdldlbecc).
2. Open a **private window** and sign in to YouTube there.
3. In that same window go to `https://www.youtube.com/robots.txt`. This page
   does nothing, which is the point: no YouTube page is open to change the
   cookies while you export them.
4. Export the cookies for `youtube.com` with the extension and save the file
   as `cookies.txt`.
5. **Close the private window** without signing out. YouTube replaces the
   cookies of a session that stays in use, and signing out ends it, so either
   would make the file useless within hours.
6. In Ghost Engine, **Settings → YouTube sign-in → Open the mpv folder**, and
   put `cookies.txt` there, next to `mpv.exe`.

That is all: the next song that fails is retried with it.

### When it stops working

Every cookie has an expiry date, and YouTube can end a session early. mpv
tells you when that is the likely reason (`Your YouTube cookies expired on …`
or `YouTube no longer accepts your cookies.txt`). Do the steps above again
and replace the file.

## Why only on a retry

Signed in, YouTube hands out stream links that some songs refuse to play. So
every song is first asked for without cookies, which is how almost all of them
play, and only a song that failed is asked for again with them. The README's
[cookies.txt](../README.md#cookiestxt) section has the details.
