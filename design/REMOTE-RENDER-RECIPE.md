# Teaching prompt — show visuals to a remote-mode user (HTML → PNG → github.com URL)

> **How to use this:** paste the section below into another Claude Code session (any repo). It teaches
> the technique and asks that session to add a condensed version to *its* `CLAUDE.md` so it sticks.

---

When I run Claude Code in **remote mode** (e.g. from my phone), I **cannot** see any of these:
`show_widget` / visualize widgets, files on your host's local drive (`C:\…`), or the "Launch preview"
panel. I **can** see two things: (a) **images you render inline with the `Read` tool** (they show as a
thumbnail in chat), and (b) **`github.com` blob URLs** I open in my logged-in browser (this works even
for a **private** repo, via my session). Use the following loop whenever you want to show me a UI mockup,
diagram, chart, wireframe, or any rendered visual.

## The workflow

1. **Author the visual as a self-contained HTML file** committed in the repo (put design artifacts under
   `design/`). Use **no external CDNs** so it renders offline and on a private repo; prefer Unicode glyphs
   over icon webfonts when it must be zero-dependency. Use real data/labels, not lorem ipsum.

2. **Render it to a PNG with headless Chrome or Edge.** Example (Windows Chrome path shown; Edge
   `msedge.exe` works the same; on macOS/Linux use the platform Chrome path):
   ```
   "C:\Program Files\Google\Chrome\Application\chrome.exe" --headless=new --disable-gpu \
     --hide-scrollbars --no-first-run --force-device-scale-factor=3 \
     --window-size=<W>,<H> --user-data-dir=<throwaway-dir> \
     --screenshot=<out.png> "file:///<absolute-path-to>.html"
   ```
   Gotchas that *will* bite you:
   - **Pin the page width and `overflow:hidden`** — e.g. `html,body{ width:200px; overflow:hidden }`.
     Otherwise one wide line of content makes the page scroll wider than the window and Chrome **clips the
     right edge** of the screenshot (the classic "half the mockup is cut off" symptom).
   - **`--window-size` height must be ≥ the rendered content height**, or the bottom clips. If your visual
     has a *fixed* target size (a device screen, a card) and the content overflows it, that's a **real
     layout bug to fix**, not just a render crop — fix the HTML.
   - **`--force-device-scale-factor=3`** renders at 3× for a crisp, zoomable PNG.
   - Use a **throwaway `--user-data-dir`** (Chrome locks the default profile) and delete it afterward.

3. **`Read` the PNG back to verify it** — this both checks the render came out right *and* shows me a
   thumbnail inline in remote mode. Iterate on the HTML until it's right.

4. **Commit the PNG to a branch and push** (commit the HTML source and a small render script too, so it's
   reproducible). Keep **one PR open** as the iteration home; re-pushing updates refreshes the same URLs.

5. **Give me the `github.com` blob URL:** `https://github.com/<owner>/<repo>/blob/<branch>/<path>.png`.
   That renders the image in the GitHub web UI under my logged-in session (private repos included).
   **Do NOT** give me a `raw.githubusercontent.com` URL — those require a token for private repos and just
   404 in a browser.

## What I want you to do now

Add a condensed version of the above to this repo's **`CLAUDE.md`** under a heading like
"Showing visuals to the user (remote mode)", adapting the browser path and the `design/` location to this
repo, so every future session here follows it without being re-told.
