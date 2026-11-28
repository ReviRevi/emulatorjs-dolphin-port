import { chromium } from '/opt/npm-tools/node_modules/playwright/index.mjs';
const b = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true, args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
const p = await b.newPage({ viewport: { width: 1000, height: 760 } });
await p.addInitScript(() => {
  const counts = globalThis.__glc = {}; let on = false; globalThis.__glcOn = (v) => { on = v; };
  const P = WebGL2RenderingContext.prototype;
  for (const k of Object.getOwnPropertyNames(P)) {
    const d = Object.getOwnPropertyDescriptor(P, k);
    if (!d || typeof d.value !== 'function') continue;
    const f = d.value; P[k] = function (...a) { if (on) counts[k] = (counts[k] || 0) + 1; return f.apply(this, a); };
  }
});
await p.goto('http://localhost:8080/?game=menu.dff');
const fn = () => p.evaluate(() => { try { return window.EJS_emulator.gameManager.getFrameNum(); } catch { return 0; } });
while ((await fn()) < 200) await p.waitForTimeout(500);
const f0 = await fn(); await p.evaluate(() => __glcOn(true)); await p.waitForTimeout(10000); await p.evaluate(() => __glcOn(false)); const f1 = await fn();
const c = await p.evaluate(() => __glc); const n = f1 - f0;
const s = await p.evaluate(() => window.EJS_emulator.Module.dolphinGLStats());
console.log('frames', n, 'total calls/frame', (Object.values(c).reduce((a, b) => a + b, 0) / n).toFixed(1));
for (const [k, v] of Object.entries(c).sort((a, b) => b[1] - a[1]).slice(0, 25)) console.log((v / n).toFixed(1).padStart(7), k);
await b.close();
