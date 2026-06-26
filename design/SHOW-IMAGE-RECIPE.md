# Teaching prompt — show me an image inline when I'm in Claude Code remote mode

> **How to use this:** paste the section below into any Claude Code session / repo. It teaches the
> session how to get an image in front of me when I'm on remote mode, and asks it to add a short version
> to that repo's `CLAUDE.md`. (For *generating* images from HTML mockups specifically, see the companion
> [`REMOTE-RENDER-RECIPE.md`](REMOTE-RENDER-RECIPE.md).)

---

When I run Claude Code in **remote mode** (e.g. from my phone), I **cannot** see: rendered widgets, the
"Launch preview" panel, or files sitting on your host's local drive — so telling me "it's saved at
`C:\…\foo.png`" doesn't help. Here's how to actually get an image in front of me. **Assume you already
have the image file.**

## Two ways

**1. Quick look (inline thumbnail) — `Read` the image file.**
Just call the `Read` tool on the image's path. The image renders inline in our chat as a thumbnail that
I can see in remote mode. Fastest way, needs nothing else — use it to show a result, screenshot, chart,
diagram, or photo.

**2. Full-size / shareable / durable — commit it and give me a `github.com` blob URL.**
- Commit the image into the repo and push it (a branch is fine).
- Send me `https://github.com/<owner>/<repo>/blob/<branch>/<path-to-image>` — that renders the image
  full-size in GitHub's web UI under my logged-in session, and it works for **private** repos too.
- **Do NOT** give me a `raw.githubusercontent.com` URL — those need a token for private repos and just
  404 in a browser. Always the `github.com/.../blob/...` form.

Use #1 for a quick inline glance; #2 when I need to open it full-size, zoom in, or keep the link.

## What I want you to do now
Add a short version of the above to this repo's `CLAUDE.md` (a heading like "Showing me images
(remote mode)") so every future session here does it automatically.
