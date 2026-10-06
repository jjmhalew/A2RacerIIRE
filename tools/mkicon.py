"""mkicon.py - draws res/a2racerre.ico: a red Dutch motorway number sign "A2" with a white border (no game art).
python tools/mkicon.py [out.ico] [preview.png]"""
import sys
from PIL import Image, ImageDraw, ImageFont


def sign(size):
    s = size * 4                                         # draw large, scale down for smooth edges
    im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = s // 16
    d.rounded_rectangle([m, s // 5, s - m, s - s // 5], radius=s // 10, fill=(255, 255, 255, 255))
    b = m + s // 28
    d.rounded_rectangle([b, s // 5 + b - m, s - b, s - s // 5 - (b - m)], radius=s // 12, fill=(206, 17, 38, 255))
    font = None
    for name in ('arialbd.ttf', 'DejaVuSans-Bold.ttf', 'Arial Bold.ttf'):
        try:
            font = ImageFont.truetype(name, int(s * 0.42))
            break
        except OSError:
            pass
    font = font or ImageFont.load_default()
    box = d.textbbox((0, 0), 'A2', font=font)
    w, h = box[2] - box[0], box[3] - box[1]
    d.text(((s - w) / 2 - box[0], (s - h) / 2 - box[1]), 'A2', font=font, fill=(255, 255, 255, 255))
    return im.resize((size, size), Image.LANCZOS)


out = sys.argv[1] if len(sys.argv) > 1 else 'res/a2racerre.ico'
big = sign(256)
big.save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
if len(sys.argv) > 2:
    big.save(sys.argv[2])
print('wrote', out)
