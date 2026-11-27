import sys
from PIL import Image
# decode the LSB-first iteration counter: XFB x = 8 + b*26 (+10 centre), y = 415; screen = 1.5x, y offset
im = Image.open(sys.argv[1]).convert('RGB')
# find the row: XFB row 400..430 -> scan for the counter band: look at screen y where pixel at x=(8+10)*1.5 is white or black inside green
v = 0
for b in range(16):
    x = int((8 + b * 26 + 10) * 1.5)
    if x >= 640: break
    px = im.getpixel((x, int(sys.argv[2]) if len(sys.argv) > 2 else 622))
    if sum(px) > 600: v |= 1 << b
print(v)
