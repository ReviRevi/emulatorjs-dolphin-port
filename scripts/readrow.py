import sys
from PIL import Image
# read 16-bit LSB-first rows at XFB rows given; screen = 1.5x, y = 190 + (row - 120)*1.5 approx
im = Image.open(sys.argv[1]).convert('RGB')
out = []
for row in map(int, sys.argv[2:]):
    y = int(190 + (row + 12 - 120) * 1.5)
    v = 0
    for b in range(16):
        x = int((8 + b * 26 + 10) * 1.5)
        if sum(im.getpixel((x, y))) > 600: v |= 1 << b
    out.append(v)
print(' '.join(f'{v:04x}' for v in out))
