import { mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { createRequire } from "node:module";

const runtimeRequire = createRequire(
  "/Users/tyg/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/.runtime.js"
);
const { chromium } = runtimeRequire("playwright");

const here = path.dirname(fileURLToPath(import.meta.url));
const htmlPath = path.join(here, "ips-130-display.html");
const outDir = path.join(here, "ips-130");

const screens = [
  ["standby-volume", "01-standby-volume.png"],
  ["standby-time", "02-standby-time.png"],
  ["confirm", "03-confirm.png"],
  ["confirm-time", "04-confirm-time.png"],
  ["running-volume", "05-running-volume.png"],
  ["running-time", "06-running-time.png"],
  ["paused", "07-paused-volume.png"],
  ["paused-time", "08-paused-time.png"],
  ["result-completed", "09-result-completed.png"],
  ["result-stopped", "10-result-stopped.png"],
  ["safety-alert", "11-safety-alert.png"],
  ["calibration-ready", "12-calibration-ready.png"],
  ["calibration-entry", "13-calibration-entry.png"],
  ["standby-offline", "14-standby-offline.png"],
  ["screen-off", "15-screen-off.png"],
];

await mkdir(outDir, { recursive: true });

const browser = await chromium.launch({ headless: true });

for (const [id, fileName] of screens) {
  const page = await browser.newPage({
    viewport: { width: 240, height: 240 },
    deviceScaleFactor: 1,
  });
  await page.goto(`file://${htmlPath}#${id}`, { waitUntil: "load" });
  await page.evaluate(() => document.fonts.ready);
  await page.waitForTimeout(1400);
  await page.screenshot({
    path: path.join(outDir, fileName),
    omitBackground: false,
  });
  await page.close();
}

const overview = await browser.newPage({
  viewport: { width: 760, height: 560 },
  deviceScaleFactor: 1,
});
await overview.goto(`file://${htmlPath}`, { waitUntil: "load" });
await overview.evaluate(() => document.fonts.ready);
await overview.waitForTimeout(1400);
await overview.screenshot({
  path: path.join(outDir, "00-overview.png"),
  fullPage: true,
  omitBackground: false,
});
await overview.close();

await browser.close();
