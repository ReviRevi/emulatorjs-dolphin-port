// Headless Chrome smoke test: loads the EmulatorJS page, logs console, screenshots.
import { chromium } from 'playwright';  // npm i playwright
const url = process.argv[2] || 'http://localhost:8080/';
const secs = Number(process.argv[3] || 40);
const out = process.argv[4] || 'shot';
const browser = await chromium.launch({
  headless: true,
  args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--autoplay-policy=no-user-gesture-required'],
});
const page = await browser.newPage({ viewport: { width: 1000, height: 760 } });
page.on('console', m => console.log(`[${m.type()}] ${m.text()}`.slice(0, 400)));
page.on('pageerror', e => console.log('[pageerror] ' + e.message));
await page.goto(url);
for (let t = 10; t <= secs; t += 10) {
  await page.waitForTimeout(10000);
  await page.screenshot({ path: `${out}-${t}s.png` });
  const fc = await page.evaluate(() => { try { return window.EJS_emulator.gameManager.getFrameNum(); } catch (e) { return 'n/a ' + e.message; } });
  console.log(`[frames @${t}s] ${fc}`);
}
await browser.close();
