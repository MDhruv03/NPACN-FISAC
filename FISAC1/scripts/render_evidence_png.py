import os
import glob
from datetime import datetime

try:
    from PIL import Image, ImageDraw, ImageFont
except Exception as exc:
    raise SystemExit("Pillow is required. Install with: python -m pip install Pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EVIDENCE_DIR = os.path.join(ROOT, "docs", "evidence")
OUT_DIR = os.path.join(EVIDENCE_DIR, "screenshots")


def render_text_to_png(src_path: str, dst_path: str) -> None:
    with open(src_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    lines = text.splitlines() or [""]
    max_lines = 120
    if len(lines) > max_lines:
        lines = lines[:max_lines] + ["... [TRUNCATED]"]

    title = f"Evidence: {os.path.basename(src_path)}"
    timestamp = datetime.now().strftime("Generated: %Y-%m-%d %H:%M:%S")
    content = [title, timestamp, ""] + lines

    # Font setup
    font = ImageFont.load_default()
    line_h = 16
    pad = 16

    width_chars = max(len(line) for line in content) if content else 80
    img_w = min(max(900, width_chars * 7 + pad * 2), 2200)
    img_h = min(max(600, len(content) * line_h + pad * 2), 2600)

    img = Image.new("RGB", (img_w, img_h), (18, 20, 24))
    draw = ImageDraw.Draw(img)

    y = pad
    for i, line in enumerate(content):
        color = (208, 214, 224)
        if i == 0:
            color = (120, 200, 255)
        elif i == 1:
            color = (150, 160, 180)
        draw.text((pad, y), line[:400], fill=color, font=font)
        y += line_h
        if y > img_h - pad:
            break

    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    img.save(dst_path)


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)

    patterns = [
        "*.txt",
        "*.json",
    ]

    files = []
    for p in patterns:
        files.extend(glob.glob(os.path.join(EVIDENCE_DIR, p)))

    if not files:
        print("[WARN] No evidence files found in docs/evidence")
        return

    for src in sorted(files):
        base = os.path.splitext(os.path.basename(src))[0]
        dst = os.path.join(OUT_DIR, f"{base}.png")
        render_text_to_png(src, dst)
        print(f"[OK] {dst}")


if __name__ == "__main__":
    main()
