// Translate dumped GLSL files with the backend's own GLSL->WGSL path (node).
import fs from 'fs';
const rt = fs.readFileSync('/home/claude/src/dolphin/Source/Core/VideoBackends/WebGPU/WGPURuntime.js', 'utf8');
const pkg = '/home/claude/src/nagapkg/x/package/dist/';
const env = { nagaGlue: fs.readFileSync('/home/claude/src/naga_glue.js', 'utf8'), nagaB64: fs.readFileSync(pkg + 'wasm/naga_bg.wasm').toString('base64') };
globalThis.atob = (s) => Buffer.from(s, 'base64').toString('binary');
const W = (new Function('env', rt))(env);
W._initNaga();
for (const f of process.argv.slice(2)) {
  let src = fs.readFileSync(f, 'utf8').replace(/^\[wgpu\] failed GLSL:\n/, '');
  const stage = /out vec4 ocol0/.test(src) ? 'fragment' : 'vertex';
  const out = W._toWgsl(src, stage);
  console.log(f, stage, out ? 'OK ' + out.length : 'FAILED');
  if (out && process.env.SHOW) console.log(out);
}
