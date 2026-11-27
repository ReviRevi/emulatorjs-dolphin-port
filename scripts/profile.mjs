// CPU profile of the emulator page (main thread) for N seconds, aggregated by function.
import { chromium } from '/opt/npm-tools/node_modules/playwright/index.mjs';
const mode = Number(process.argv[2] ?? 2);
const game = process.argv[3] ?? 'cpubench.dol';
const b = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true,
  args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const p = await b.newPage({ viewport: { width: 1000, height: 760 } });
await p.addInitScript((m) => { globalThis.DOLPHIN_WASM_JIT = m; }, mode);
await p.goto(`http://localhost:8080/?game=${game}`);
for (;;) { await p.waitForTimeout(500); const f = await p.evaluate(() => { try { return window.EJS_emulator.gameManager.getFrameNum(); } catch { return 0; } }); if (f > 200) break; }
const s = await p.context().newCDPSession(p);
await s.send('Profiler.enable');
await s.send('Profiler.setSamplingInterval', { interval: 200 });
await s.send('Profiler.start');
await p.waitForTimeout(8000);
const { profile } = await s.send('Profiler.stop');
const self = new Map(); let total = 0;
const byId = new Map(profile.nodes.map((n) => [n.id, n]));
const counts = new Map();
for (const id of profile.samples) counts.set(id, (counts.get(id) || 0) + 1);
for (const [id, n] of counts) { const node = byId.get(id); const name = node.callFrame.functionName || '(anon ' + node.callFrame.url.split('/').pop() + ')'; self.set(name, (self.get(name) || 0) + n); total += n; }
const top = [...self].sort((a, b) => b[1] - a[1]).slice(0, 40);
for (const [n, c] of top) console.log(((100 * c) / total).toFixed(1).padStart(5) + '%  ' + n.slice(0, 110));
await b.close();
