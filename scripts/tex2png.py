import json, base64, sys
from PIL import Image
import numpy as np
for f in sys.argv[1:]:
    r = json.load(open(f)); d = np.frombuffer(base64.b64decode(r['b64']), dtype=np.uint8)
    if r['fmt'] in ('rgba8unorm', 'bgra8unorm'):
        a = d.reshape(r['h'], r['w'], 4)[:, :, :3]
        if r['fmt'] == 'bgra8unorm': a = a[:, :, ::-1]
    else:
        a = np.frombuffer(d.tobytes(), dtype=np.float32).reshape(r['h'], r['w']); a = (np.clip(a, 0, 1) * 255).astype(np.uint8)
    Image.fromarray(np.ascontiguousarray(a)).save(f.replace('.json', '.png')); print(f, a.mean())
