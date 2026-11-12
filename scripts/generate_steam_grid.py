#!/usr/bin/env python3
import os
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUTPUT_DIR = "steam/grid"
os.makedirs(OUTPUT_DIR, exist_ok=True)

FONT_PATH = "resources/font/font.ttf"
if not os.path.exists(FONT_PATH):
    FONT_PATH = "/System/Library/Fonts/Supplemental/Arial.ttf"

def get_font(size):
    try:
        return ImageFont.truetype(FONT_PATH, size)
    except Exception:
        return ImageFont.load_default()

def render_muvik_mark(size=512, body_color=(242, 245, 248, 255), screen_color=(27, 40, 56, 255), play_color=(102, 192, 244, 255)):
    # Render at 4x for super sampling
    ss = 4
    s_size = size * ss
    scale = s_size / 512.0
    im = Image.new("RGBA", (s_size, s_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    def s(val):
        return val * scale

    # 1. Grips & Body
    draw.rounded_rectangle([s(96), s(170), s(96 + 320), s(170 + 150)], radius=s(56), fill=body_color)
    draw.rounded_rectangle([s(92), s(248), s(92 + 104), s(248 + 104)], radius=s(44), fill=body_color)
    draw.rounded_rectangle([s(316), s(248), s(316 + 104), s(248 + 104)], radius=s(44), fill=body_color)

    # 2. Screen
    draw.rounded_rectangle([s(150), s(196), s(150 + 212), s(196 + 98)], radius=s(14), fill=screen_color)

    # 3. Play triangle
    draw.polygon([(s(220), s(202)), (s(220), s(288)), (s(294), s(245))], fill=play_color)

    bbox = im.getbbox()
    cropped = im.crop(bbox)
    
    # Downsample with Lanczos
    target_w = int(cropped.width / ss)
    target_h = int(cropped.height / ss)
    return cropped.resize((target_w, target_h), Image.Resampling.LANCZOS)

def draw_text_tracked(draw, xy, text, font, fill, tracking=0):
    x, y = xy
    for char in text:
        draw.text((x, y), char, font=font, fill=fill)
        bbox = draw.textbbox((x, y), char, font=font)
        w = bbox[2] - bbox[0]
        x += w + tracking

def measure_text_tracked(draw, text, font, tracking=0):
    total_w = 0
    max_h = 0
    for char in text:
        bbox = draw.textbbox((0, 0), char, font=font)
        w = bbox[2] - bbox[0]
        h = bbox[3] - bbox[1]
        total_w += w + tracking
        if h > max_h:
            max_h = h
    if total_w > tracking:
        total_w -= tracking
    return total_w, max_h

# 1. Generate Logo (800x300, transparent)
def generate_logo():
    w, h = 800, 300
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    mark = render_muvik_mark(360, body_color=(245, 248, 252, 255), screen_color=(20, 28, 40, 255), play_color=(56, 178, 255, 255))
    mark_w, mark_h = mark.size

    # Position mark on left
    mark_x = 70
    mark_y = int((h - mark_h) / 2)
    im.paste(mark, (mark_x, mark_y), mark)

    # Title "Muvik"
    title_font = get_font(96)
    sub_font = get_font(20)

    text_x = mark_x + mark_w + 40
    title_y = int(h / 2) - 68

    # Subtle drop shadow
    draw.text((text_x + 2, title_y + 3), "Muvik", font=title_font, fill=(10, 15, 25, 120))
    draw.text((text_x, title_y), "Muvik", font=title_font, fill=(255, 255, 255, 255))

    sub_y = title_y + 104
    draw_text_tracked(draw, (text_x + 2, sub_y), "UNIVERSAL MEDIA PLAYER", font=sub_font, fill=(100, 180, 245, 240), tracking=5)

    return im

# 2. Generate Vertical Capsule / Library Poster (600x900)
def generate_poster():
    w, h = 600, 900
    im = Image.new("RGBA", (w, h), (14, 20, 28, 255))
    
    # Background vertical gradient
    gradient = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(gradient)
    for y in range(h):
        ratio = y / h
        # top: #182434 -> middle: #131d2a -> bottom: #0b1017
        r = int(24 * (1 - ratio) + 11 * ratio)
        g = int(36 * (1 - ratio) + 16 * ratio)
        b = int(52 * (1 - ratio) + 23 * ratio)
        gdraw.line([(0, y), (w, y)], fill=(r, g, b, 255))
    im = Image.alpha_composite(im, gradient)

    # Ambient radial spotlight in upper center
    spot = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(spot)
    cx, cy = 300, 380
    max_r = 320
    for r in range(max_r, 0, -6):
        alpha = int(45 * (1.0 - (r / max_r)**1.4))
        sdraw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(26, 140, 255, alpha))
    im = Image.alpha_composite(im, spot)

    # Handheld Mark (large)
    mark = render_muvik_mark(540, body_color=(245, 248, 252, 255), screen_color=(18, 26, 38, 255), play_color=(56, 178, 255, 255))
    mw, mh = mark.size
    mx = int((w - mw) / 2)
    my = 260
    im.paste(mark, (mx, my), mark)

    draw = ImageDraw.Draw(im)

    # Title "Muvik"
    title_font = get_font(72)
    sub_font = get_font(18)
    badge_font = get_font(15)

    tw, th = measure_text_tracked(draw, "Muvik", title_font)
    tx = int((w - tw) / 2)
    ty = my + mh + 50

    # Glow under text
    draw.text((tx, ty + 2), "Muvik", font=title_font, fill=(0, 0, 0, 160))
    draw.text((tx, ty), "Muvik", font=title_font, fill=(255, 255, 255, 255))

    sub_text = "UNIVERSAL MEDIA PLAYER"
    sw, sh = measure_text_tracked(draw, sub_text, sub_font, tracking=4)
    sx = int((w - sw) / 2)
    sy = ty + 80
    draw_text_tracked(draw, (sx, sy), sub_text, font=sub_font, fill=(110, 190, 255, 240), tracking=4)

    # Bottom badge
    badge_text = "STEAM DECK & HANDHELD OPTIMIZED"
    bw, bh = measure_text_tracked(draw, badge_text, badge_font, tracking=3)
    bx = int((w - bw) / 2)
    by = h - 70
    draw.line([(bx - 20, by - 12), (bx + bw + 20, by - 12)], fill=(255, 255, 255, 30), width=1)
    draw_text_tracked(draw, (bx, by), badge_text, font=badge_font, fill=(160, 175, 195, 200), tracking=3)

    return im

# 3. Generate Horizontal Capsule / Header Banner (920x430)
def generate_capsule():
    w, h = 920, 430
    im = Image.new("RGBA", (w, h), (12, 17, 24, 255))

    # Horizontal dark gradient
    gradient = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(gradient)
    for x in range(w):
        ratio = x / w
        r = int(12 * (1 - ratio) + 22 * ratio)
        g = int(17 * (1 - ratio) + 33 * ratio)
        b = int(24 * (1 - ratio) + 48 * ratio)
        gdraw.line([(x, 0), (x, h)], fill=(r, g, b, 255))
    im = Image.alpha_composite(im, gradient)

    # Ambient radial spotlight behind center-left
    spot = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(spot)
    cx, cy = 320, 215
    max_r = 300
    for r in range(max_r, 0, -8):
        alpha = int(40 * (1.0 - (r / max_r)**1.4))
        sdraw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(26, 140, 255, alpha))
    im = Image.alpha_composite(im, spot)

    # Mark
    mark = render_muvik_mark(420, body_color=(245, 248, 252, 255), screen_color=(18, 26, 38, 255), play_color=(56, 178, 255, 255))
    mw, mh = mark.size
    mx = 120
    my = int((h - mh) / 2)
    im.paste(mark, (mx, my), mark)

    draw = ImageDraw.Draw(im)

    title_font = get_font(84)
    sub_font = get_font(19)
    badge_font = get_font(15)

    tx = mx + mw + 50
    ty = int(h / 2) - 62
    draw.text((tx, ty + 2), "Muvik", font=title_font, fill=(0, 0, 0, 160))
    draw.text((tx, ty), "Muvik", font=title_font, fill=(255, 255, 255, 255))

    sub_text = "UNIVERSAL MEDIA PLAYER"
    sy = ty + 94
    draw_text_tracked(draw, (tx, sy), sub_text, font=sub_font, fill=(110, 190, 255, 240), tracking=4)

    desc_text = "High Performance Video & Audio for Steam Deck"
    draw.text((tx, sy + 34), desc_text, font=badge_font, fill=(160, 175, 195, 200))

    return im

# 4. Generate Hero Banner (1920x620)
def generate_hero():
    w, h = 1920, 620
    im = Image.new("RGBA", (w, h), (10, 15, 22, 255))

    # Gradient: darker on left (where Steam overlays UI buttons/title), cinematic glow on right
    gradient = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(gradient)
    for x in range(w):
        ratio = x / w
        r = int(10 * (1 - ratio) + 25 * ratio)
        g = int(14 * (1 - ratio) + 38 * ratio)
        b = int(20 * (1 - ratio) + 56 * ratio)
        gdraw.line([(x, 0), (x, h)], fill=(r, g, b, 255))
    im = Image.alpha_composite(im, gradient)

    # Ambient radial highlights on right half
    spot = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(spot)
    cx, cy = 1350, 310
    max_r = 450
    for r in range(max_r, 0, -10):
        alpha = int(40 * (1.0 - (r / max_r)**1.4))
        sdraw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(26, 140, 255, alpha))
    im = Image.alpha_composite(im, spot)

    # Handheld Mark in right area (watermark / accent style)
    mark = render_muvik_mark(600, body_color=(245, 248, 252, 220), screen_color=(18, 26, 38, 240), play_color=(56, 178, 255, 255))
    mw, mh = mark.size
    mx = 1350 - int(mw / 2)
    my = int((h - mh) / 2)
    im.paste(mark, (mx, my), mark)

    # Left vignette to guarantee 100% legibility of Steam game title and Play button
    vignette = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    vdraw = ImageDraw.Draw(vignette)
    for x in range(800):
        alpha = int(160 * (1.0 - (x / 800.0)))
        vdraw.line([(x, 0), (x, h)], fill=(10, 14, 20, alpha))
    im = Image.alpha_composite(im, vignette)

    return im

if __name__ == "__main__":
    print("Generating Muvik Steam Grid Art...")
    logo = generate_logo()
    poster = generate_poster()
    capsule = generate_capsule()
    hero = generate_hero()

    logo.save(f"{OUTPUT_DIR}/logo.png")
    poster.save(f"{OUTPUT_DIR}/poster.png")
    capsule.save(f"{OUTPUT_DIR}/capsule.png")
    hero.save(f"{OUTPUT_DIR}/hero.png")

    for appid in ["3963364876", "3663855200"]:
        logo.save(f"{OUTPUT_DIR}/{appid}_logo.png")
        poster.save(f"{OUTPUT_DIR}/{appid}p.png")
        capsule.save(f"{OUTPUT_DIR}/{appid}.png")
        hero.save(f"{OUTPUT_DIR}/{appid}_hero.png")
    print("Generated all files in", OUTPUT_DIR)
