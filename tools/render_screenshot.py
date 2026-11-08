#!/usr/bin/env python3
"""
tools/render_screenshot.py
Generate pixel-perfect PNG screenshots of the ESP32 CYD Mission Clock
using the exact 8x16 1bpp VGA font bitmaps from main/term_font_8x16.c.
"""

import os
import re
from PIL import Image, ImageDraw

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_C_PATH = os.path.join(BASE_DIR, "main", "term_font_8x16.c")
OUT_DIR = os.path.join(BASE_DIR, "docs", "images")
os.makedirs(OUT_DIR, exist_ok=True)

# Parse 8x16 font bitmaps from C source
def load_font():
    with open(FONT_C_PATH, "r") as f:
        content = f.read()
    
    # Match 96 arrays of 16 hex bytes
    pattern = re.compile(r'\{\s*(0x[0-9a-fA-F]{2}(?:\s*,\s*0x[0-9a-fA-F]{2}){15})\s*\}')
    glyphs = []
    for m in pattern.finditer(content):
        bytes_str = m.group(1)
        byte_vals = [int(b.strip(), 16) for b in bytes_str.split(",")]
        glyphs.append(byte_vals)
    return glyphs

GLYPHS = load_font()

# Colors
COLOR_BG            = (0, 0, 0)
COLOR_BRIGHT_GREEN  = (0, 255, 65)
COLOR_DIM_GREEN     = (0, 170, 51)
COLOR_DARK_GREEN    = (0, 119, 34)
COLOR_BANNER_GREEN  = (0, 170, 51)
COLOR_DIVIDER       = (0, 110, 35)
COLOR_WHITE         = (255, 255, 255)
COLOR_DIM_WHITE     = (170, 170, 170)

def draw_char(img, cx, cy, char_code, fg_color):
    idx = char_code - 0x20
    if idx < 0 or idx >= len(GLYPHS):
        idx = 0
    bitmap = GLYPHS[idx]
    
    for row in range(16):
        y = cy + row
        if y >= img.height:
            continue
        byte_val = bitmap[row]
        for col in range(8):
            x = cx + col
            if x >= img.width:
                continue
            if (byte_val >> (7 - col)) & 1:
                img.putpixel((x, y), fg_color)

def draw_text(img, x, y, segments):
    """
    segments: list of (text, (r, g, b))
    """
    cur_x = x
    for text, color in segments:
        for ch in text:
            draw_char(img, cur_x, y, ord(ch), color)
            cur_x += 8

def render_screen(lines, status_clock, status_mission, status_ip):
    # 320x240 screen
    img = Image.new("RGB", (320, 240), COLOR_BG)
    draw = ImageDraw.Draw(img)
    
    # 11 Log Rows (Y = 2 + row * 19)
    for r, segs in enumerate(lines[:11]):
        y = 2 + r * 19
        draw_text(img, 4, y, segs)
        
    # Divider at Y = 213 (2px height)
    draw.rectangle([0, 213, 319, 214], fill=COLOR_DIVIDER)
    
    # Status row at Y = 220
    # Left: Clock
    draw_text(img, 4, 220, [(status_clock, COLOR_BRIGHT_GREEN)])
    # Center: Mission / Mode
    draw_text(img, 116, 220, [(status_mission, COLOR_DIM_GREEN)])
    # Right: IP Address
    draw_text(img, 196, 220, [(status_ip, COLOR_DARK_GREEN)])
    
    return img

def create_crt_frame(screen_img, scale=2):
    """
    Wrap the 320x240 screen in a retro terminal bezel frame with subtle scanlines.
    """
    w = screen_img.width * scale
    h = screen_img.height * scale
    scaled = screen_img.resize((w, h), Image.NEAREST)
    
    # Add scanlines
    overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y in range(0, h, 2):
        for x in range(w):
            overlay.putpixel((x, y), (0, 0, 0, 35))
            
    scaled.paste(overlay, (0, 0), overlay)
    
    # Add retro monitor border
    border_x = 24
    border_y = 24
    frame_w = w + border_x * 2
    frame_h = h + border_y * 2
    
    frame = Image.new("RGB", (frame_w, frame_h), (20, 22, 24)) # Dark bezel
    draw = ImageDraw.Draw(frame)
    
    # Bezel inner shadow
    draw.rectangle([border_x - 3, border_y - 3, border_x + w + 2, border_y + h + 2], outline=(10, 10, 10), width=2)
    # Bezel outer rounded border
    draw.rounded_rectangle([2, 2, frame_w - 3, frame_h - 3], radius=12, outline=(50, 54, 58), width=3)
    
    frame.paste(scaled, (border_x, border_y))
    return frame

def generate_screenshots():
    # 1. Apollo 17 Pre-Launch Hold in [GET] Mode
    a17_lines = [
        [("--- MISSION INITIALIZED: Apollo 17 ---", COLOR_BANNER_GREEN)],
        [("-02:26:58", COLOR_DARK_GREEN), (" ", (0,0,0)), ("PAO", COLOR_DIM_GREEN), (" ", (0,0,0)), ("This is Apollo Saturn", COLOR_BRIGHT_GREEN)],
        [("  Control, we're T minus 1", COLOR_BRIGHT_GREEN)],
        [("  hour 12 minutes and", COLOR_BRIGHT_GREEN)],
        [("  counting.", COLOR_BRIGHT_GREEN)],
        [("-02:26:54", COLOR_DARK_GREEN), (" ", (0,0,0)), ("PAO", COLOR_DIM_GREEN), (" ", (0,0,0)), ("At this time Spacecraft", COLOR_BRIGHT_GREEN)],
        [("  Commander Gene Cernan and", COLOR_BRIGHT_GREEN)],
        [("  Skip Chauvin are going", COLOR_BRIGHT_GREEN)],
        [("  over some command checks.", COLOR_BRIGHT_GREEN)],
        [("-02:26:14", COLOR_DARK_GREEN), (" ", (0,0,0)), ("PAO", COLOR_DIM_GREEN), (" ", (0,0,0)), ("This is Kennedy Launch", COLOR_BRIGHT_GREEN)],
        [("  Control.", COLOR_BRIGHT_GREEN), ("\x7f", COLOR_BRIGHT_GREEN)] # Block cursor
    ]
    img1 = render_screen(a17_lines, "21:49:50 JST", "A17 [GET]", "192.168.0.232")
    img1.save(os.path.join(OUT_DIR, "screenshot_a17_get_raw.png"))
    frame1 = create_crt_frame(img1, scale=2)
    frame1.save(os.path.join(OUT_DIR, "screenshot_a17_get.png"))
    print("Saved screenshot_a17_get.png")

    # 2. Apollo 11 Lunar Landing in [LOC] Mode with Hourly Joke in White
    a11_lines = [
        [(" 20:17:39", COLOR_DARK_GREEN), (" ", (0,0,0)), ("CDR", COLOR_DIM_GREEN), (" ", (0,0,0)), ("Contact light.", COLOR_BRIGHT_GREEN)],
        [(" 20:17:42", COLOR_DARK_GREEN), (" ", (0,0,0)), ("CC ", COLOR_DIM_GREEN), (" ", (0,0,0)), ("Roger, Tranquility. We", COLOR_BRIGHT_GREEN)],
        [("  copy you on the ground.", COLOR_BRIGHT_GREEN)],
        [(" 20:17:45", COLOR_DARK_GREEN), (" ", (0,0,0)), ("CDR", COLOR_DIM_GREEN), (" ", (0,0,0)), ("Houston, Tranquility", COLOR_BRIGHT_GREEN)],
        [("  Base here. The Eagle has", COLOR_BRIGHT_GREEN)],
        [("  landed.", COLOR_BRIGHT_GREEN)],
        [(" 20:17:58", COLOR_DARK_GREEN), (" ", (0,0,0)), ("CC ", COLOR_DIM_GREEN), (" ", (0,0,0)), ("Roger, TQ, we copy you on", COLOR_BRIGHT_GREEN)],
        [("  the ground. You got a", COLOR_BRIGHT_GREEN)],
        [("  bunch of guys about to", COLOR_BRIGHT_GREEN)],
        [("  turn blue. Thanks a lot.", COLOR_BRIGHT_GREEN)],
        [(" 21:00:04", COLOR_DIM_WHITE), (" ", (0,0,0)), ("CDR", COLOR_WHITE), (" ", (0,0,0)), ("Houston, do you have", COLOR_WHITE), ("\x7f", COLOR_WHITE)]
    ]
    img2 = render_screen(a11_lines, "21:00:05 JST", "A11 [LOC]", "192.168.0.232")
    img2.save(os.path.join(OUT_DIR, "screenshot_a11_loc_raw.png"))
    frame2 = create_crt_frame(img2, scale=2)
    frame2.save(os.path.join(OUT_DIR, "screenshot_a11_loc.png"))
    print("Saved screenshot_a11_loc.png")

    # 3. Mission Completed & 1-Hour Recovery Intermission
    comp_lines = [
        [(" 23:40:15", COLOR_DARK_GREEN), (" ", (0,0,0)), ("CC ", COLOR_DIM_GREEN), (" ", (0,0,0)), ("Welcome home, America.", COLOR_BRIGHT_GREEN)],
        [(" 23:40:20", COLOR_DARK_GREEN), (" ", (0,0,0)), ("CDR", COLOR_DIM_GREEN), (" ", (0,0,0)), ("Thank you, Houston. What", COLOR_BRIGHT_GREEN)],
        [("  a ride.", COLOR_BRIGHT_GREEN)],
        [("--- MISSION COMPLETE: Apollo 17 ---", COLOR_BANNER_GREEN)],
        [("--- 1-HR RECOVERY: NEXT Apollo 8 ---", COLOR_BANNER_GREEN), ("\x7f", COLOR_BRIGHT_GREEN)]
    ]
    img3 = render_screen(comp_lines, "23:45:00 JST", "A17 [WAIT]", "192.168.0.232")
    img3.save(os.path.join(OUT_DIR, "screenshot_a17_complete_raw.png"))
    frame3 = create_crt_frame(img3, scale=2)
    frame3.save(os.path.join(OUT_DIR, "screenshot_a17_complete.png"))
    print("Saved screenshot_a17_complete.png")

if __name__ == "__main__":
    generate_screenshots()
