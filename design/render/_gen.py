"""Render the LCD UI prototype HTML to PNGs via headless Chrome (one per screen + a contact sheet)."""
import pathlib
import subprocess

ROOT = pathlib.Path(r"C:\repos\SB20-power-proxy")
SRC = ROOT / "design" / "sb20-lcd-ui-v1.html"
REND = ROOT / "design" / "render"
REND.mkdir(parents=True, exist_ok=True)
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

src = SRC.read_text(encoding="utf-8")
style = src[src.index("<style>"):src.index("</style>") + len("</style>")]

cells = []
needle = '<div class="cell">'
i = 0
while True:
    s = src.find(needle, i)
    if s < 0:
        break
    depth, j = 0, s
    while j < len(src):
        no = src.find("<div", j)
        nc = src.find("</div>", j)
        if nc < 0:
            break
        if no != -1 and no < nc:
            depth += 1
            j = no + 4
        else:
            depth -= 1
            j = nc + 6
            if depth == 0:
                cells.append(src[s:j])
                i = j
                break
    else:
        break
print("cells found:", len(cells))


def page(inner, pad=10):
    return (f"<!DOCTYPE html><html><head><meta charset='utf-8'>{style}</head>"
            f"<body style='margin:0;background:#0a0c14;padding:{pad}px'>{inner}</body></html>")


def shot(html_path, png_path, w, h):
    subprocess.run([
        CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
        "--no-first-run", "--no-default-browser-check",
        "--force-device-scale-factor=3", f"--window-size={w},{h}",
        f"--user-data-dir={REND / '_cud'}", f"--screenshot={png_path}",
        html_path.as_uri(),
    ], check=True, timeout=90)
    print("wrote", png_path.name, png_path.stat().st_size, "bytes")


for n, c in enumerate(cells, 1):
    p = REND / f"screen-{n}.html"
    p.write_text(page(f'<div class="gal" style="background:none;padding:0">{c}</div>'), encoding="utf-8")
    shot(p, REND / f"screen-{n}.png", 210, 392)

shot(SRC, REND / "all.png", 430, 1340)
print("done")
