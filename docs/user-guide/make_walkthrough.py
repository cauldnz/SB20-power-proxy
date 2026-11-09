#!/usr/bin/env python3
"""make_walkthrough.py: build the narrated walkthrough video from real captures.

Every device frame here is a framebuffer dump from a running board (`bench_ui.py`), and every web
page is the page that board served, so the video cannot drift into a mock-up of something that does
not exist. The one exception is the tiny OLED, drawn from the text `OledScreen.h` lays out, because
no camera was on it. The narration says so.

    python docs/user-guide/make_walkthrough.py --frames <dir> --out walkthrough.mp4

Windows only, for the voice (System.Speech). Needs Pillow and ffmpeg.
"""

from __future__ import annotations

import argparse
import subprocess
import tempfile
import wave
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H, FPS = 1920, 1080, 15
FADE_FRAMES = 6
BG = (10, 16, 28)
FG = (236, 240, 247)
MUTED = (150, 162, 182)
ACCENT = (64, 132, 246)
BEZEL = (28, 34, 46)
VOICE = "Microsoft Hazel Desktop"
FFMPEG = r"C:\ProgramData\chocolatey\bin\ffmpeg.exe"
FONT_DIR = Path(r"C:\Windows\Fonts")


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(FONT_DIR / ("segoeuib.ttf" if bold else "segoeui.ttf")), size)


def wrap(draw: ImageDraw.ImageDraw, text: str, f: ImageFont.FreeTypeFont, width: int) -> list[str]:
    lines: list[str] = []
    for para in text.split("\n"):
        cur = ""
        for word in para.split():
            trial = f"{cur} {word}".strip()
            if draw.textlength(trial, font=f) <= width:
                cur = trial
            else:
                lines.append(cur)
                cur = word
        lines.append(cur)
    return lines


# --- what a scene shows -------------------------------------------------------------------------


@dataclass
class Shot:
    """One image on screen for `share` of the scene, with an optional tap ripple at (x, y)."""

    image: str
    share: float = 1.0
    tap: tuple[int, int] | None = None


@dataclass
class Scene:
    title: str
    say: str
    caption: str
    shots: list[Shot] = field(default_factory=list)
    device: str = "guition"  # guition | phone | oled | none
    tag: str = ""  # which board this step applies to


def device_panel(frames: Path, shot: Shot, device: str) -> Image.Image:
    img = Image.open(frames / shot.image).convert("RGB")
    if device == "guition":
        scale = 2
        pw, ph = img.width * scale, img.height * scale
        out = Image.new("RGB", (pw + 56, ph + 56), BEZEL)
        out.paste(img.resize((pw, ph), Image.LANCZOS), (28, 28))
    elif device == "phone":
        ph = 960
        pw = 470
        scaled = img.resize((pw, int(img.height * pw / img.width)), Image.LANCZOS).crop((0, 0, pw, ph))
        out = Image.new("RGB", (pw + 36, ph + 56), BEZEL)
        out.paste(scaled, (18, 28))
    elif device == "oled":
        out = img
    else:
        out = img
    return out


def compose(frames: Path, scene: Scene, shot: Shot | None) -> tuple[Image.Image, tuple[int, int, float]]:
    """The full 1080p frame for one shot, and where a tap lands on it."""
    canvas = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(canvas)
    tap_at = (0, 0, 0.0)
    text_x = 110
    text_w = W - 220
    if shot is not None:
        panel = device_panel(frames, shot, scene.device)
        px = 120
        py = (H - panel.height) // 2
        canvas.paste(panel, (px, py))
        text_x = px + panel.width + 120
        text_w = W - text_x - 110
        if shot.tap and scene.device == "guition":
            tap_at = (px + 28 + shot.tap[0] * 2, py + 28 + shot.tap[1] * 2, 1.0)
    y = 250 if shot is not None else 330
    if scene.tag:
        tf = font(30, bold=True)
        draw.text((text_x, y - 62), scene.tag.upper(), font=tf, fill=ACCENT)
    tf = font(70, bold=True)
    for line in wrap(draw, scene.title, tf, text_w):
        draw.text((text_x, y), line, font=tf, fill=FG)
        y += 88
    y += 26
    cf = font(38)
    for line in wrap(draw, scene.caption, cf, text_w):
        draw.text((text_x, y), line, font=cf, fill=MUTED)
        y += 54
    return canvas, tap_at


# --- voice --------------------------------------------------------------------------------------


def speak(text: str, path: Path) -> float:
    safe = text.replace("'", "''")
    script = (
        "Add-Type -AssemblyName System.Speech;"
        "$s=New-Object System.Speech.Synthesis.SpeechSynthesizer;"
        f"$s.SelectVoice('{VOICE}');$s.Rate=0;"
        f"$s.SetOutputToWaveFile('{path}');$s.Speak('{safe}');$s.Dispose()"
    )
    subprocess.run(["powershell.exe", "-NoProfile", "-Command", script], check=True)
    with wave.open(str(path), "rb") as w:
        return w.getnframes() / w.getframerate()


# --- assembly -----------------------------------------------------------------------------------


def ripple(frame: Image.Image, x: int, y: int, age: float) -> Image.Image:
    out = frame.copy()
    draw = ImageDraw.Draw(out, "RGBA")
    r = int(26 + 44 * age)
    alpha = int(210 * (1 - age))
    draw.ellipse((x - r, y - r, x + r, y + r), outline=(255, 255, 255, alpha), width=6)
    draw.ellipse((x - 18, y - 18, x + 18, y + 18), fill=(255, 255, 255, alpha // 2))
    return out


def scene_frames(frames: Path, scene: Scene, seconds: float):
    """Yield raw 1080p frames for one scene."""
    total = max(1, round(seconds * FPS))
    shots = scene.shots or [None]
    shares = sum(s.share for s in scene.shots) if scene.shots else 1.0
    counts = [max(1, round(total * ((s.share if s else 1.0) / shares))) for s in shots]
    prev: Image.Image | None = None
    for shot, count in zip(shots, counts):
        img, (tx, ty, has) = compose(frames, scene, shot)
        raw = img.tobytes()
        for i in range(count):
            if prev is not None and i < FADE_FRAMES:
                yield Image.blend(prev, img, (i + 1) / FADE_FRAMES).tobytes()
            elif has and i < FADE_FRAMES + 9:
                yield ripple(img, tx, ty, max(0.0, (i - FADE_FRAMES) / 9)).tobytes()
            else:
                yield raw
        prev = img


def build(scenes: list[Scene], frames: Path, out: Path) -> None:
    with tempfile.TemporaryDirectory() as tmpd:
        tmp = Path(tmpd)
        wavs: list[Path] = []
        durations: list[float] = []
        for i, scene in enumerate(scenes):
            wav = tmp / f"s{i:02d}.wav"
            seconds = speak(scene.say, wav)
            wavs.append(wav)
            durations.append(seconds + 0.7)
            print(f"  scene {i + 1:2d}: {seconds:5.1f}s  {scene.title}")
        audio = tmp / "voice.wav"
        with wave.open(str(audio), "wb") as w:
            for i, wav in enumerate(wavs):
                with wave.open(str(wav), "rb") as r:
                    if i == 0:
                        w.setparams(r.getparams())
                    w.writeframes(r.readframes(r.getnframes()))
                    pad = int(0.7 * r.getframerate()) * r.getsampwidth() * r.getnchannels()
                    w.writeframes(b"\0" * pad)
        ff = subprocess.Popen(
            [FFMPEG, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
             "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-i", str(audio),
             "-c:v", "libx264", "-preset", "medium", "-crf", "23", "-pix_fmt", "yuv420p",
             "-c:a", "aac", "-b:a", "96k", "-shortest", "-movflags", "+faststart", str(out)],
            stdin=subprocess.PIPE,
        )
        assert ff.stdin
        for scene, seconds in zip(scenes, durations):
            for raw in scene_frames(frames, scene, seconds):
                ff.stdin.write(raw)
        ff.stdin.close()
        if ff.wait() != 0:
            raise SystemExit("ffmpeg failed")
    print(f"{out}: {sum(durations):.0f}s, {out.stat().st_size / 1e6:.1f} MB")


def main() -> int:
    import storyboard  # the scene list, next to this file

    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--frames", required=True, type=Path)
    p.add_argument("--out", default=Path("walkthrough.mp4"), type=Path)
    a = p.parse_args()
    build(storyboard.scenes(Shot, Scene), a.frames, a.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
