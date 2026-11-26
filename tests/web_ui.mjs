// Prueba en navegador (Chrome) del prototipo web: salida, medidores, deshacer y presets.
// Usa la TDM que escribe la prueba de humo del plugin para comprobar que los presets son compatibles.
//   node webtest.mjs <html> <TDM.excellecu del plugin> <carpeta capturas>
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
import fs from 'node:fs';

import path from 'node:path';
const [htmlArg, pluginTdm, outDir] = process.argv.slice(2);
const html = path.resolve(htmlArg);
let fails = 0;
const check = (ok, what) => { console.log((ok ? '  ok  ' : '  FALLO ') + what); if (!ok) fails++; };

const browser = await chromium.launch({ executablePath: process.env.CHROME || '/usr/bin/google-chrome', args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage({ viewport: { width: 1300, height: 1250 } });
const errors = [];
page.on('pageerror', e => errors.push(e.message));
page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
page.on('dialog', d => d.accept(d.type() === 'prompt' ? 'Mi preset' : undefined));
await page.goto('file://' + html);
await page.waitForTimeout(300);
check(errors.length === 0, 'carga sin errores ' + errors.join(' | '));

// ── deshacer con un arrastre real en la gráfica ──
const cv = await page.$('#cMag');
const box = await cv.boundingBox();
await page.mouse.dblclick(box.x + box.width * 0.5, box.y + box.height * 0.4);
await page.waitForTimeout(50);
check(await page.evaluate(() => bands.length === 1 && undoStack.length === 1), 'doble clic crea banda = 1 paso');
const pt = await page.evaluate(() => { const G = magGeom(); const b = bands[0], p = b.sets.st; return { x: G.toX(p.fc), y: G.toY(p.gain) }; });
const scale = box.width / (await page.evaluate(() => document.getElementById('cMag').getBoundingClientRect().width));
await page.mouse.move(box.x + pt.x * scale, box.y + pt.y);
await page.mouse.down();
for (let i = 1; i <= 15; i++) await page.mouse.move(box.x + pt.x * scale + i * 8, box.y + pt.y + i * 2);
await page.mouse.up();
await page.waitForTimeout(50);
const afterDrag = await page.evaluate(() => ({ n: undoStack.length, fc: bands[0].sets.st.fc }));
check(afterDrag.n === 2, 'un arrastre entero = 1 paso (' + afterDrag.n + ')');
await page.keyboard.press('Control+z');
await page.waitForTimeout(30);
const undone = await page.evaluate(() => bands[0].sets.st.fc);
check(Math.abs(undone - afterDrag.fc) > 1, 'Ctrl+Z deshace el arrastre');
await page.click('#btn-undo');
check(await page.evaluate(() => bands.length === 0), 'botón Deshacer quita la banda');
await page.keyboard.press('Control+Shift+z');
await page.keyboard.press('Control+y');
check(await page.evaluate((fc) => bands.length === 1 && Math.abs(bands[0].sets.st.fc - fc) < 1e-6, afterDrag.fc), 'rehacer recupera banda y arrastre');

// ── salida ──
await page.click('#out-ms');
await page.$eval('#sl-out-b', el => { el.dispatchEvent(new Event('pointerdown')); el.value = 2; el.dispatchEvent(new Event('input')); el.dispatchEvent(new Event('change', { bubbles: true })); });
await page.waitForTimeout(30);
let og = await page.evaluate(() => ({ ...outGain }));
check(og.mode === 'ms' && og.s === 2 && og.m === 2, 'M/S enlazado: Side +2 mueve Mid +2');
await page.uncheck('#out-link');
await page.$eval('#sl-out-a', el => { el.dispatchEvent(new Event('pointerdown')); el.value = -1; el.dispatchEvent(new Event('input')); el.dispatchEvent(new Event('change', { bubbles: true })); });
await page.click('#out-lr');
await page.$eval('#sl-out-a', el => { el.dispatchEvent(new Event('pointerdown')); el.value = -3; el.dispatchEvent(new Event('input')); el.dispatchEvent(new Event('change', { bubbles: true })); });
await page.waitForTimeout(30);
og = await page.evaluate(() => ({ ...outGain, note: document.getElementById('out-note').textContent }));
check(og.m === -1 && og.s === 2 && og.l === -3 && og.r === 0, 'sin enlace y en L/R: Mid −1, Side +2, L −3');
check(og.note.includes('Mid -1.0') && og.note.includes('Side +2.0'), 'aviso de M/S activa: ' + og.note);
const num = await page.evaluate(() => {
  const a = new Float32Array([1, 0.5]), b = new Float32Array([1, -0.5]);
  const [L, R] = applyOutGain([a, b]);
  // muestra 0: L=R → solo Mid; muestra 1: L=−R → solo Side
  const gl = 10 ** (-3 / 20), gm = 10 ** (-1 / 20), gs = 10 ** (2 / 20);
  const m0 = 0.5 * (gl + 1) * gm, s0 = 0.5 * (gl - 1) * gs;
  return [L[0], m0 + s0, R[0], m0 - s0];
});
check(Math.abs(num[0] - num[1]) < 1e-6 && Math.abs(num[2] - num[3]) < 1e-6, 'applyOutGain = L/R y luego M/S');
check(await page.evaluate(() => undoStack.length) >= 5, 'los cambios de salida entran en el historial');

// ── presets ──
await page.selectOption('#preset-sel', { label: 'TDM' });
await page.waitForTimeout(50);
let st = await page.evaluate(() => ({ n: bands.length, e2: bands[2].edit, e3: bands[3].edit, g: bands[3].sets.m.gain, out: { ...outGain }, mod: document.getElementById('preset-mod').textContent }));
check(st.n === 5 && st.e2 === 's' && st.e3 === 'm' && st.g === 1, 'TDM: 5 bandas, 3 en Side, 4 en Mid +1 dB');
check(st.out.l === 0 && st.out.m === 0 && st.mod === '', 'TDM: salida a 0 dB y sin «modificado»');
// La TDM del plugin (archivo) da exactamente las mismas curvas
const same = await page.evaluate((xml) => {
  const a = parsePresetXml(xml);
  const strip = bs => JSON.stringify(bs.map(b => ({ type: b.type, edit: b.edit, sets: b.sets, lrFresh: b.lrFresh })));
  return strip(a.bands) === strip(bands);
}, fs.readFileSync(pluginTdm, 'utf8'));
check(same, 'el archivo TDM.excellecu del plugin abre igual que la TDM de la web');
const rt = await page.evaluate(() => {
  const x = presetXml('rt', { bands, out: outGain });
  const p = parsePresetXml(x);
  return presetXml('rt', p) === x;
});
check(rt, 'guardar y volver a leer un preset da el mismo XML');
await page.$eval('#sl-out-a', el => { el.dispatchEvent(new Event('pointerdown')); el.value = 1.5; el.dispatchEvent(new Event('input')); el.dispatchEvent(new Event('change', { bubbles: true })); });
await page.waitForTimeout(30);
check((await page.textContent('#preset-mod')).includes('modificado'), 'tocar algo marca «modificado»');
await page.click('#preset-save');   // el diálogo responde "Mi preset"
await page.waitForTimeout(50);
st = await page.evaluate(() => ({ name: presetName, list: presetList().map(e => e.name), mod: document.getElementById('preset-mod').textContent }));
check(st.name === 'Mi preset' && st.list.includes('Mi preset') && st.mod === '', 'guardar preset propio (localStorage)');
await page.click('#preset-prev');
await page.waitForTimeout(30);
check(await page.evaluate(() => presetName === 'TDM'), 'preset anterior');
await page.click('#btn-undo');
check(await page.evaluate(() => bands.length === 5 && outGain.l === 1.5), 'deshacer la carga de un preset');
// Importar un archivo del plugin
await page.setInputFiles('#preset-file', pluginTdm);
await page.waitForTimeout(100);
check(await page.evaluate(() => presetName === 'TDM (importado)' && bands.length === 5), 'importar TDM.excellecu (preset de fábrica del plugin)');

// ── medidores con reproducción ──
await page.evaluate(() => {
  const sr = 48000, n = sr * 3;
  const ctx = new AudioContext({ sampleRate: sr });
  const buf = ctx.createBuffer(2, n, sr);
  const L = buf.getChannelData(0), R = buf.getChannelData(1);
  for (let i = 0; i < n; i++) { L[i] = 0.9 * Math.sin(2 * Math.PI * 1000 * i / sr); R[i] = 0.5 * Math.sin(2 * Math.PI * 1000 * i / sr); }
  ctx.close();
  rawBuffer = buf;
  processedPCM = [L.slice(), R.slice()];
  outGain.l = 3; outGain.r = 0; outGain.m = 0; outGain.s = 0; syncOut();
  document.getElementById('btn-proc').disabled = false;
});
await page.click('#btn-proc');
await page.waitForTimeout(700);
const mt = await page.evaluate(() => ({ rmsR: meters.out[1].rms, inL: meters.in[0].peak, outL: meters.out[0].peak, outR: meters.out[1].peak, clip: meters.out[0].clip, inClip: meters.in[0].clip, playing: !!playStart }));
console.log('     medidores', JSON.stringify(mt));
check(mt.playing && Math.abs(mt.inL - 20 * Math.log10(0.9)) < 0.5, 'medidor de entrada L ≈ −0,9 dBFS');
check(Math.abs(mt.outL - (20 * Math.log10(0.9) + 3)) < 0.6 && mt.clip && !mt.inClip, 'salida L ≈ +2,1 dBFS con +3 dB: saturación solo en la salida');
check(Math.abs(mt.outR - 20 * Math.log10(0.5)) < 0.5, 'salida R ≈ −6 dBFS');
check(Math.abs(mt.rmsR - 20 * Math.log10(0.5 / Math.SQRT2)) < 0.1, 'RMS de salida R ≈ −9,03 dBFS (' + mt.rmsR.toFixed(2) + ')');
await page.screenshot({ path: outDir + '/web_v021.png', fullPage: false, clip: { x: 0, y: 280, width: 1300, height: 760 } });
await page.click('#btn-proc');
await page.click('#cMeter');
check(await page.evaluate(() => !meters.out[0].clip), 'clic en el medidor borra la saturación');

check(errors.length === 0, 'sin errores en la consola ' + errors.join(' | '));
await browser.close();
console.log(fails ? fails + ' fallo(s)' : 'TODO BIEN');
process.exit(fails ? 1 : 0);
