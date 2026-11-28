import { chromium } from '/opt/npm-tools/node_modules/playwright/index.mjs';
const dual = process.argv[2] ?? '1';
const b = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true, args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
const p = await b.newPage();
await p.addInitScript((d) => { globalThis.DOLPHIN_GL_DUALSRC = Number(d); }, dual);
await p.goto('http://localhost:8080/?game=fpbench.dol&opts=' + encodeURIComponent(JSON.stringify({dolphin_shader_compilation_mode:'1',dolphin_wait_for_shaders:'enabled'})));
const fn = () => p.evaluate(() => { try { return window.EJS_emulator.gameManager.getFrameNum(); } catch { return 0; } });
for (let i = 0; i < 400 && (await fn()) < 60; i++) await p.waitForTimeout(500);
const r = await p.evaluate(() => { const M = window.EJS_emulator.Module; return { stats: M.dolphinGLStats(), bad: (M.dolphinBadShaders||[]).map(s => ({ what: s.what, log: s.log.split('\n').filter(Boolean).slice(0, 6) })) }; });
console.log(JSON.stringify(r.stats));
const seen = new Set();
for (const s of r.bad) { const k = s.log.join('|'); if (seen.has(k)) continue; seen.add(k); console.log(s.what, s.log); }
await b.close();
