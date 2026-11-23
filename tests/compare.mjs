// Compara la salida del procesado original de la web (JS) con la del motor en C++.
//   node tests/compare.mjs <carpeta>
import fs from 'node:fs';
import path from 'node:path';

const dir = process.argv[2] || 'tests/out';
const names = JSON.parse(fs.readFileSync(path.join(dir, 'scenarios.json'), 'utf8'));
const TOL = 1e-4;
let fail = 0;
for (const n of names) {
  const js = new Float32Array(fs.readFileSync(path.join(dir, n + '.js.f32')).buffer.slice(0));
  const cp = new Float32Array(fs.readFileSync(path.join(dir, n + '.cpp.f32')).buffer.slice(0));
  if (js.length !== cp.length) { console.log(`✗ ${n}: longitud distinta ${js.length} vs ${cp.length}`); fail++; continue; }
  let maxDiff = 0, at = -1, peak = 0, energy = 0;
  for (let i = 0; i < js.length; i++) {
    const d = Math.abs(js[i] - cp[i]);
    if (d > maxDiff) { maxDiff = d; at = i; }
    peak = Math.max(peak, Math.abs(js[i]));
    energy += js[i] * js[i];
  }
  const ok = maxDiff <= TOL;
  if (!ok) fail++;
  console.log(`${ok ? '✓' : '✗'} ${n.padEnd(10)} dif. máx ${maxDiff.toExponential(2)} (muestra ${at})  pico ${peak.toFixed(3)}  rms ${Math.sqrt(energy / js.length).toFixed(4)}`);
}
console.log(fail ? `${fail} escenario(s) con diferencias` : 'todos los escenarios coinciden');
process.exit(fail ? 1 : 0);
