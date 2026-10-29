"""Generates res/snow.ico: a white snowflake with a thin dark outline
so the icon reads on both light and dark taskbars."""
import math
import os
from PIL import Image, ImageDraw

SS = 4  # supersampling for anti-aliasing


def segments(size, detailed):
    """Snowflake line segments in [0..size] coordinates."""
    c = size / 2
    R = size * 0.46
    segs = []
    for k in range(6):
        a = math.radians(90 + k * 60)
        dx, dy = math.cos(a), -math.sin(a)
        segs.append(((c, c), (c + dx * R, c + dy * R), 1.0))
        branches = [(0.42, 0.30), (0.70, 0.20)] if detailed else [(0.52, 0.30)]
        for pos, ln in branches:
            bx, by = c + dx * R * pos, c + dy * R * pos
            for s in (-1, 1):
                b = a + s * math.radians(48)
                ex, ey = bx + math.cos(b) * R * ln, by - math.sin(b) * R * ln
                segs.append(((bx, by), (ex, ey), 0.8))
    return segs


def draw_flake(size):
    detailed = size >= 32
    big = size * SS
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    base_w = max(1.6, size * (0.075 if detailed else 0.11))
    outline = max(1.0, size * 0.035)

    def stroke(width, color):
        for (x0, y0), (x1, y1), k in segments(size, detailed):
            w = width * k * SS
            p0 = (x0 * SS, y0 * SS)
            p1 = (x1 * SS, y1 * SS)
            d.line([p0, p1], fill=color, width=max(1, int(round(w))))
            r = w / 2
            for (px, py) in (p0, p1):
                d.ellipse([px - r, py - r, px + r, py + r], fill=color)

    stroke(base_w + outline * 2, (28, 40, 66, 210))
    stroke(base_w, (255, 255, 255, 255))
    c = big / 2
    r = base_w * SS * 0.9
    d.ellipse([c - r, c - r, c + r, c + r], fill=(255, 255, 255, 255))
    return img.resize((size, size), Image.LANCZOS)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "res", "snow.ico")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sizes = [256, 64, 48, 40, 32, 24, 20, 16]
    imgs = [draw_flake(s) for s in sizes]
    imgs[0].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[1:])
    imgs[0].save(os.path.join(root, "res", "snow_preview.png"))
    print("icon written:", out)


if __name__ == "__main__":
    main()
