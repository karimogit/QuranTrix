import { chromium } from 'playwright';
import { mkdir } from 'fs/promises';
import path from 'path';
import { fileURLToPath } from 'url';

const dir = path.dirname(fileURLToPath(import.meta.url));
const html = path.join(dir, 'qurantrix.html');
const outDir = path.join(dir, 'preview');
await mkdir(outDir, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage();
await page.goto('file://' + html);
await page.evaluate(() => document.body.classList.remove('locked'));

for (const mode of ['today', 'ramadan']) {
  await page.evaluate((m) => {
    previewMode = m;
    drawPaperCanvas();
  }, mode);
  await page.locator('#prevCanvas').screenshot({
    path: path.join(outDir, `dashboard-${mode}.png`),
    type: 'png'
  });
}

await browser.close();
console.log(outDir);
