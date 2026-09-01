// freestanding スモークテストの実行: node freestanding_smoke.js <wasm>
const fs = require('fs');

const bytes = fs.readFileSync(process.argv[2]);
const mod = new WebAssembly.Module(bytes);
const inst = new WebAssembly.Instance(mod, {});
const ret = inst.exports.run();
if (ret !== 0) {
  console.error(`freestanding smoke FAILED: run() = ${ret}`);
  process.exit(1);
}
console.log('freestanding smoke OK');
