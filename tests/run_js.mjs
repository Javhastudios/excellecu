// Ejecuta el procesado ORIGINAL de la web (web/eq-dft-proto.html, función processAudio) con
// varios escenarios de bandas y guarda la salida. compare_cpp hace lo mismo con el motor en
// C++ (en tiempo real) y compare.mjs compara ambas.
//
//   node tests/run_js.mjs <carpeta-salida>
//
// La web rellena con silencio el principio, así que su resultado es "en régimen" desde la
// primera muestra: se compara el archivo entero. Cada escenario se ejecuta con los dos motores
// de la web v0.18: Clásico (nombre tal cual) y Exacto (prefijo "x_").
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const outDir = process.argv[2] || path.join(here, 'out');
fs.mkdirSync(outDir, { recursive: true });

const SR = 48000;

// ── señal de prueba determinista (estéreo, con golpes y ruido) ──
function lcg(seed) { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296 - 0.5; }; }
const FRAMES = Math.round(SR * 1.2);
const rnd = lcg(7);
const L = new Float32Array(FRAMES), R = new Float32Array(FRAMES);
for (let i = 0; i < FRAMES; i++) {
  const t = i / SR;
  let v = 0.25 * Math.sin(2 * Math.PI * 110 * t) + 0.12 * Math.sin(2 * Math.PI * 1250 * t) + 0.08 * Math.sin(2 * Math.PI * 7300 * t) + 0.1 * rnd();
  for (const h of [0.0, 0.4, 0.9]) { const d = t - h; if (d >= 0) v += 0.5 * Math.exp(-d * 25) * Math.sin(2 * Math.PI * 3000 * d); }
  L[i] = v;
  R[i] = 0.6 * v + 0.2 * Math.sin(2 * Math.PI * 450 * t) + 0.08 * rnd();
}
{
  const buf = Buffer.alloc(4 + FRAMES * 8);
  buf.writeInt32LE(FRAMES, 0);
  Buffer.from(L.buffer).copy(buf, 4);
  Buffer.from(R.buffer).copy(buf, 4 + FRAMES * 4);
  fs.writeFileSync(path.join(outDir, 'in.f32'), buf);
}

// ── escenarios: bandas creadas con makeBand de la web y retocadas ──
// { type, edit, st: {...}, l: {...}, r: {...}, m: {...}, s: {...} }
const scenarios = {
  vacio: { N: 4096, overlap: 0.5, bands: [] },
  estereo: { N: 8192, overlap: 0.75, bands: [
    { type: 'bell', st: { fc: 1000, gain: 6, q: 1.5 } },
    { type: 'ls', st: { fc: 150, gain: -4, q: 0.8 } },
    { type: 'hs', st: { fc: 6000, gain: 3.5, q: 2 } },
  ] },
  filtros: { N: 1024, overlap: 0.25, bands: [
    { type: 'hp', st: { fc: 120, slope: 24 } },
    { type: 'lp', st: { fc: 9000, slope: 12 } },
    { type: 'bell', st: { fc: 2500, gain: -9, q: 4 } },
    { type: 'lp', st: { fc: 15000, slope: 48, on: false } },
  ] },
  lr: { N: 4096, overlap: 0.5, bands: [
    { type: 'bell', edit: 'l', l: { fc: 500, gain: 6, q: 2 }, r: { fc: 2000, gain: -4, q: 1 } },
    { type: 'hs', st: { fc: 8000, gain: -6, q: 0.7 } },
    { type: 'hp', edit: 'r', l: { fc: 40, slope: 12 }, r: { fc: 300, slope: 48 } },
  ] },
  ms: { N: 512, overlap: 0.75, bands: [
    { type: 'bell', edit: 'm', m: { fc: 300, gain: 5, q: 1 }, s: { fc: 300, gain: 0 } },
    { type: 'hs', edit: 's', s: { fc: 6000, gain: 6, q: 1 } },
    { type: 'ls', st: { fc: 100, gain: 3, q: 1 } },
    { type: 'hp', edit: 's', m: { on: false }, s: { on: true, fc: 300, slope: 48 } },
  ] },
  lr_ms: { N: 4096, overlap: 0.5, bands: [
    { type: 'bell', edit: 'r', l: { fc: 800, gain: -6, q: 3 }, r: { fc: 800, gain: 4, q: 3 } },
    { type: 'lp', edit: 'm', m: { on: true, fc: 12000, slope: 24 }, s: { on: true, fc: 4000, slope: 6 } },
    { type: 'bell', st: { fc: 60, gain: 8, q: 0.5 } },
  ] },
  mono: { mono: true, N: 4096, overlap: 0.5, bands: [
    { type: 'bell', st: { fc: 3000, gain: -10, q: 2 } },
    { type: 'hs', edit: 'm', m: { fc: 5000, gain: 5, q: 1 } },
    { type: 'bell', edit: 'l', l: { fc: 200, gain: 6, q: 1 }, r: { fc: 200, gain: -6, q: 1 } },
  ] },
};

// ── cargar el <script> de la página con un DOM de mentira ──
const html = fs.readFileSync(path.join(here, '..', 'web', 'eq-dft-proto.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const any = new Proxy(function () {}, {
  get: (t, p) => (p === Symbol.toPrimitive ? () => 0 : p === 'width' ? 100 : any),
  set: () => true,
  apply: () => any,
});
function element() {
  return {
    style: { setProperty() {} }, classList: { add() {}, remove() {}, toggle() {} }, addEventListener() {},
    appendChild() {}, contains: () => false, setPointerCapture() {},
    parentElement: { getBoundingClientRect: () => ({ width: 820 }) },
    getBoundingClientRect: () => ({ width: 820, height: 160, left: 0, top: 0 }),
    getContext: () => any, textContent: '', innerHTML: '', value: '0', width: 0, height: 0,
  };
}

const KEYS = ['st', 'l', 'r', 'm', 's'];
const TYPES = ['bell', 'ls', 'hs', 'hp', 'lp'];
const names = [];
// Escenario extra para el motor exacto: el caso medido en Plugin Doctor (HPF 48 dB/oct en graves)
scenarios.hpf_graves = { N: 8192, overlap: 0.75, bands: [
  { type: 'hp', st: { fc: 50.3, slope: 48 } },
  { type: 'bell', st: { fc: 64, gain: 5, q: 1 } },
  { type: 'bell', edit: 's', s: { fc: 100, gain: -6, q: 1 } },
] };
const runs = [];
for (const [name, sc] of Object.entries(scenarios)) {
  runs.push([name, sc, 'classic']);
  runs.push(['x_' + name, sc, 'exact']);
}
for (const [name, sc, engineMode] of runs) {
  const ctx = vm.createContext({
    document: { getElementById: () => element(), createElement: () => element(), activeElement: null },
    window: { addEventListener() {}, devicePixelRatio: 1, innerWidth: 1200, innerHeight: 800 },
    requestAnimationFrame: () => 0,
    setTimeout: (f) => { f(); return 0; },
    Math, Float32Array, Float64Array, Promise, console, Set, Object, Array, JSON, String, Number, isFinite,
  });
  vm.runInContext(script, ctx);
  const numCh = sc.mono ? 1 : 2;
  ctx.__buf = { sampleRate: SR, numberOfChannels: numCh, getChannelData: (c) => (c === 0 ? L : R) };
  ctx.__spec = sc;
  ctx.__engine = engineMode;
  vm.runInContext(`
    N = __spec.N; overlap = __spec.overlap; engine = __engine; bands = []; nextBandId = 1;
    for (const s of __spec.bands) {
      const b = makeBand(s.type, s.st || {});
      bands.push(b);
      b.edit = s.edit || 'st';
      for (const k of ['l','r','m','s']) if (s[k]) Object.assign(b.sets[k], s[k]);
    }
    rawBuffer = __buf;`, ctx);
  await vm.runInContext('processAudio()', ctx);
  const outCh = vm.runInContext('processedPCM', ctx);
  const bands = JSON.parse(vm.runInContext('JSON.stringify(bands)', ctx));

  const out = new Float32Array(FRAMES * numCh);
  for (let c = 0; c < numCh; c++) out.set(outCh[c], c * FRAMES);
  fs.writeFileSync(path.join(outDir, name + '.js.f32'), Buffer.from(out.buffer));

  // Guion para el lado C++, con TODOS los juegos tal como quedaron en la web
  const lines = [`channels ${numCh}`, `N ${sc.N}`, `overlap ${sc.overlap}`, `engine ${engineMode}`];
  bands.forEach((b, slot) => {
    lines.push(`band ${slot} ${TYPES.indexOf(b.type)} ${KEYS.indexOf(b.edit)}`);
    KEYS.forEach((k, ki) => {
      const p = b.sets[k];
      lines.push(`set ${slot} ${ki} ${p.on ? 1 : 0} ${p.fc} ${p.gain} ${p.q} ${p.slope}`);
    });
  });
  fs.writeFileSync(path.join(outDir, name + '.txt'), lines.join('\n') + '\n');
  names.push(name);
  console.log(`JS ${name}: motor ${engineMode}, ${numCh} canal(es), ${bands.length} banda(s)`);
}
fs.writeFileSync(path.join(outDir, 'scenarios.json'), JSON.stringify(names));
