#!/usr/bin/env python3
"""
Convert Linux kernel font_8x16 to LVGL 1bpp font.
"""

import urllib.request
import re

url = "https://raw.githubusercontent.com/torvalds/linux/master/lib/fonts/font_8x16.c"
raw = urllib.request.urlopen(url).read().decode('utf-8', errors='ignore')

# Extract hex values from fontdata_8x16
# 256 characters * 16 bytes = 4096 bytes
hex_matches = re.findall(r'0x([0-9a-fA-F]{2})', raw)
print(f"Found {len(hex_matches)} hex bytes in fontdata_8x16")
assert len(hex_matches) >= 4096

bytes_data = [int(h, 16) for h in hex_matches[:4096]]

# We only need ASCII 0x20 (' ') to 0x7E ('~') -> 95 glyphs
# Each glyph is 16 bytes
c_out = []
c_out.append('#include "term_font_8x16.h"\n')
c_out.append('// Standard crisp 8x16 monospace ASCII glyphs (1px stroke, non-scaled)')
c_out.append('// Stored as 16 rows of 1 byte each (8 bits per row = 8 horizontal pixels)')
c_out.append('static const uint8_t s_font_8x16_bitmaps[95][16] = {')

for code in range(0x20, 0x7F):
    char_name = chr(code)
    if char_name == '\\': char_desc = "'\\\\'"
    elif char_name == "'": char_desc = "'\\''"
    else: char_desc = f"'{char_name}'"
    
    offset = code * 16
    char_bytes = bytes_data[offset : offset + 16]
    hex_str = ", ".join(f"0x{b:02X}" for b in char_bytes)
    c_out.append(f"    // 0x{code:02X} {char_desc}")
    c_out.append(f"    {{{hex_str}}},")

c_out.append('};\n')

c_out.append('''static bool term_font_8x16_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                         uint32_t unicode_letter, uint32_t unicode_letter_next)
{
    (void)font;
    (void)unicode_letter_next;

    if (unicode_letter == '\\t') {
        unicode_letter = ' ';
    }
    if (unicode_letter < 0x20 || unicode_letter > 0x7E) {
        return false;
    }

    dsc_out->adv_w = 8;   // 8px character advance width
    dsc_out->box_w = 8;   // 8px box width (1 byte per row)
    dsc_out->box_h = 16;  // 16 rows high
    dsc_out->ofs_x = 0;
    dsc_out->ofs_y = 0;
    dsc_out->bpp   = 1;
    dsc_out->is_placeholder = false;
    return true;
}

static const uint8_t *term_font_8x16_get_glyph_bitmap(const lv_font_t *font, uint32_t unicode_letter)
{
    (void)font;
    if (unicode_letter == '\\t') {
        unicode_letter = ' ';
    }
    if (unicode_letter < 0x20 || unicode_letter > 0x7E) {
        return s_font_8x16_bitmaps[0];
    }
    return s_font_8x16_bitmaps[unicode_letter - 0x20];
}

const lv_font_t lv_font_term_8x16 = {
    .get_glyph_dsc = term_font_8x16_get_glyph_dsc,
    .get_glyph_bitmap = term_font_8x16_get_glyph_bitmap,
    .line_height = 16,
    .base_line = 0,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = 0,
    .underline_thickness = 0,
    .dsc = NULL
};
''')

with open("/home/ken/git/esp_missionclock/main/term_font_8x16.c", "w") as f:
    f.write("\n".join(c_out))

print("Successfully generated /home/ken/git/esp_missionclock/main/term_font_8x16.c")
