#!/usr/bin/env python3
"""Export gint's bundled bitmap font without linking another runtime."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
im = Image.open('/tools/codex/workspace/gint-reference/src/font8x9.png').convert('RGBA')
rows = []
for n in range(95):
    ox, oy = (n % 16)*10+1, (n//16)*13+1
    bits = []
    for y in range(11):
        bits.append(sum((1 << x) for x in range(8)
                        if im.getpixel((ox+x,oy+y))[:3] == (0,0,0)
                        and im.getpixel((ox+x,oy+y))[3]))
    width = max((v.bit_length() for v in bits), default=0) or 4
    rows.append('    {' + ','.join(map(str,[width]+bits)) + '}')
(root/'apps/CasioWIFI/src/ui_font.h').write_text(
    '/* Bitmap glyphs from gint-reference/src/font8x9.png. */\n'
    'static const unsigned char ui_font[95][12] = {\n'+',\n'.join(rows)+'\n};\n')
