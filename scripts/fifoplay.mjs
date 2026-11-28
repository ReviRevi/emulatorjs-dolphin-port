import { chromium } from '/opt/npm-tools/node_modules/playwright/index.mjs';
const game = process.argv[2] || 'menu.dff';
const out = process.argv[3] || '/home/claude/out/fifo.png';
const b = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true, args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
const p = await b.newPage({ viewport: { width: 1000, height: 760 } });
const extra = process.env.GLOBALS ? JSON.parse(process.env.GLOBALS) : {};
await p.addInitScript((g) => { Object.assign(globalThis, g); }, extra);
p.on('console', (m) => { const t = m.text(); if (!/invalid core option|404|TUNNEL/.test(t)) console.log('console:', t.slice(0, 300)); });
p.on('crash', () => console.log('PAGE CRASHED')); p.on('pageerror', (e) => console.log('pageerror', e.message, (e.stack||'').split('\n').slice(0,14).join(' | ')));
await p.goto(`http://localhost:8080/?game=${game}` + (process.env.OPTS ? '&opts=' + encodeURIComponent(process.env.OPTS) : ''));
const fn = () => p.evaluate(() => { try { return window.EJS_emulator.gameManager.getFrameNum(); } catch { return 0; } });
for (let i = 0; i < 60; i++) { await p.waitForTimeout(1000); if (i % 10 == 0) console.log('t', i, await fn()); }
console.log('frames', await fn(), JSON.stringify(await p.evaluate(() => window.EJS_emulator.Module.dolphinGLStats?.())));
await p.screenshot({ path: out });
await b.close();
