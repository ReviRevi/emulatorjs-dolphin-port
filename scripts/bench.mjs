// Benchmark: run cpubench.dol with a given JIT mode; report core fps and a
// screenshot at a fixed emulated frame.
import { chromium } from 'playwright';  // npm i playwright
const mode = Number(process.argv[2] ?? 2);
const atFrame = Number(process.argv[3] ?? 700);
const game = process.argv[4] ?? 'cpubench.dol';
const b = await chromium.launch({ headless: true,
  args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const p = await b.newPage({ viewport: { width: 1000, height: 760 } });
p.on('pageerror', (e) => console.log('pageerror', e.message));
p.on('console', (m) => { const t = m.text(); if (/WebAssembly JIT|RuntimeError|Aborted/.test(t)) console.log(t.slice(0, 300)); });
await p.addInitScript((m) => { globalThis.DOLPHIN_WASM_JIT = m; }, mode);
await p.goto(`http://localhost:8080/?game=${game}`);
const frames = () => p.evaluate(() => { try { return window.EJS_emulator.gameManager.getFrameNum(); } catch { return 0; } });
let f0 = 0, t0 = 0;
for (;;) {
  await p.waitForTimeout(250);
  const f = await frames();
  if (!t0 && f >= 100) { f0 = f; t0 = Date.now(); }
  if (f >= atFrame) {
    const fps = ((f - f0) / ((Date.now() - t0) / 1000)).toFixed(1);
    await p.screenshot({ path: `./bench-m${mode}.png` });
    const jit = await p.evaluate(() => (window.EJS_emulator.Module.wasmJitBlocks || 0) + ' fallbacks ' + JSON.stringify(window.EJS_emulator.Module.wasmJitFallbacks));
    console.log(`mode ${mode}: ${fps} core fps (frames ${f0}->${f}), compiled blocks ${jit}`);
    break;
  }
}
await b.close();
