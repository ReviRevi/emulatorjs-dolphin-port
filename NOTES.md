# Developer notes

Working notes for continuing this port (written for whoever picks it up next — including Claude).

## Pinned versions

| Component | Revision |
|---|---|
| libretro/dolphin | `a91b1d311d4d57757282e0644a63bfd100d8d232` (2026-11-25) |
| EmulatorJS/RetroArch | `97f2cfa65d01d57406e4ce9459242c8ada7c7e3b` |
| EmulatorJS/EmulatorJS | `5a65bde5d3b47c75f22e90453c3c14c469411589` (4.3.0-pre) |
| Emscripten | 3.1.52 (LLVM 18, Binaryen 115) |

EmulatorJS's own core builds use emsdk 3.1.74; 3.1.52 was used here only because it matches
the system LLVM 18 in the build VM. Moving to 3.1.74 should be fine (watch for the
`.worker.js` file disappearing — EmulatorJS handles both layouts).

## How the core is put together

1. Dolphin is configured with `LIBRETRO=ON ENABLE_GENERIC=ON` (no JIT, `_M_GENERIC`) and
   built with `emcmake` into static libs; `dolphin_libretro` is made `STATIC` under Emscripten.
2. All ~43 static libs are merged with `emar -M` into `dolphin_libretro_emscripten.a`, copied
   to `RetroArch/emulatorjs/dolphin_libretro_emscripten.bc`.
3. `RetroArch/emulatorjs/build-emulatorjs.sh --threads` links it with the EmulatorJS
   RetroArch frontend. `dolphin` is in the `largeStack`, `largeHeap`, `needsGles3`,
   `needsThreads`, `largeThreads`, `noCHD` lists → 128 MB stack, 512 MB initial heap
   (growable to 4 GB), 32 pre-spawned pthreads, WebGL2/GLES3, no CHD.
4. The resulting `dolphin_libretro.{js,wasm,worker.js}` + `core.json`/`license.txt`/`build.json`
   are 7z'd into `dolphin-thread-wasm.data` (~3 MB compressed, ~10 MB wasm).
5. EmulatorJS downloads `cores/dolphin-sys.zip` and unpacks it to
   `/home/web_user/retroarch/system/dolphin-emu/Sys`; RetroArch's `system_directory` is set
   to `/home/web_user/retroarch/system` for this core.

## Dolphin changes (patches/dolphin.patch)

Most changes reuse paths the libretro fork already has for Switch 2 / webOS (no mmap, no JIT,
32-bit). Everything is behind `__EMSCRIPTEN__`/`EMSCRIPTEN`.

- **Memory arena**: `Common/MemArenaEmscripten.cpp` (copy of the Switch 2 arena) backs
  emulated RAM with one allocation; no views, no fastmem region. `MemArena.h` exposes
  `m_backing` for Emscripten.
- **Boot defaults** (`DolphinLibretro/Boot.cpp`): fastmem off, CPU core forced to Cached
  Interpreter (Interpreter allowed), **single-core mode forced**. Reason: in dual-core mode
  `retro_run` blocks the browser main thread waiting for the GPU/CPU threads, and Emscripten
  proxies worker filesystem syscalls *to* the main thread → deadlock risk. Single-core runs
  the whole emulator inside `retro_run` (`CPU::RunSingleFrame`).
- **GL** (`VideoContexts/GLContextLR.cpp`): function pointers come from
  `emscripten_webgl2_get_proc_address` first; the generic lookup returned WebGL1 `*EXT`
  variants (e.g. `glGenQueriesEXT`) that crash on a WebGL2 context.
- **Stream buffers** (`OGL/OGLStreamBuffer.cpp`): `BufferSubData`/`BufferData` re-bind their
  buffer before uploading. WebGL lacks BaseVertex so these fallbacks are used, and the
  element-array binding is per-VAO → "bufferSubData: no buffer".
- Platform `#if` fixes: `Timer.cpp` (CLOCK_MONOTONIC), `DriverDetails.cpp` (OS_ALL),
  `Socket.h` / `AMMediaboard.cpp` / `EXI_DeviceEthernet.h` (POSIX socket branch),
  `Thread.cpp` (no `pthread_setname_np`), `MemoryUtil.cpp` (no `sysinfo`).
- **libusb**: null backend under Emscripten (`Externals/libusb/CMakeLists.txt`).
- **zlib-ng**: configure with `-DWITH_OPTIM=OFF` (Emscripten's toolchain reports x86).
- **SFML**: treat Emscripten as Linux (`patches/dolphin-sfml.patch`).

Link with `ERROR_ON_UNDEFINED_SYMBOLS=0` (RetroArch's default!) means any missing libc
function becomes a *runtime abort*. After each link, check:
`grep -o "undefined symbol: .*" rabuild.log | sort -u` — it must be empty.

## Test results (2026-11-27)

- Headless Chrome (SwiftShader software GL, 2 vCPU VM): core loads, Dolphin OGL backend
  initialises on WebGL2 ("OpenGL ES 3.0 (WebGL 2.0)"), `colorbars.dol` boots and animates.
  ~16 emulated frames/s for that trivial program on that machine. Real GPU + desktop CPU
  should be considerably faster; commercial games not yet tested.
- Missing GL features reported by Dolphin on WebGL2: DualSourceBlend, PrimitiveRestart,
  EarlyZ, BufferStorage, BaseVertex, ClipControl, DepthClamp, CopyImageSubData, MSAA/SSAA,
  GS instancing. Expect some graphical inaccuracies (blending/depth edge cases).
- "Failed to create shared context for shader compiling" warnings are expected: no shared
  GL contexts on the web, so shaders compile synchronously on the main thread (stutter).

## WebAssembly JIT (Source/Core/Core/PowerPC/CachedInterpreter/CachedInterpreterWasm.cpp)

A tier on top of the Cached Interpreter (Emscripten only):

- Every block's callback list starts with a `WasmEntry` callback holding `{fn, calls, block}`.
  `ExecuteOneBlock` counts runs; at `WASM_JIT_THRESHOLD` (16) the block is compiled.
- `CompileWasm` walks the callback list and emits one WebAssembly function per block
  (`WasmEmitter.h` builds the module: imports the main module's table + shared memory).
  JS glue (`EM_JS`) does `new WebAssembly.Module` + `Instance`, puts the export in the table
  (`-sALLOW_TABLE_GROWTH`, see the RetroArch patch) and returns the index; freed in
  `OnDestroyBlock` (hooked into `CachedInterpreterBlockCache::DestroyBlock`).
- The function returns the next block's table index (0 = back to the dispatcher). With
  WebAssembly tail calls (detected at run time) it `return_call_indirect`s into it instead.
  A block that branches to itself loops inside its own function.
- Inline code (`EmitInline`): integer ALU / compare / rotate / shift / mul / div / carry ops,
  D- and X-form loads and stores (u8/u16/u32, update forms) with a RAM fast path that mirrors
  `MMU::ReadFromHardware`/`WriteToHardware` (BAT translation, RAM/EXRAM; page tables, MMIO,
  locked L1, dcache emulation, page-crossing and odd cache-inhibited stores go to the
  Interpreter), `b`/`bc`/`bclr`/`bcctr`, and `EndBlock` (no perf-monitor blocks).
  Everything else calls the Interpreter's function (fallback).
- GPRs are cached in locals per block; flushed before Interpreter calls/callbacks/exits.
  Rule: never load a register for the first time inside a conditional path.
- MSR.DR is a compile-time constant per block (blocks are keyed by DR/IR; MSR writers end blocks).
- Only runs on the thread that compiled (tables are per thread): the web build runs the CPU on
  the browser main thread (single-core mode).

Controls / diagnostics (page JavaScript, in the emulator's frame):
- `globalThis.DOLPHIN_WASM_JIT = 0 | 1 | 2` before the game starts: off / calls only / inline (default).
- `EJS_emulator.Module.wasmJitBlocks`: blocks compiled. `EJS_emulator.Module.wasmJitFallbacks`:
  per-instruction count of compiled call sites that still use the Interpreter (what to inline next).

Test + benchmark: `test-content/cpubench` (green = 32 results identical to the native build of the
same C; binary counter = iterations). `scripts/bench.mjs <mode> <frame>` reports emulated fps.
Results in the build VM (2 vCPU, software WebGL), steady state:

| Mode | cpubench emulated fps |
|---|---|
| Cached Interpreter (JIT off) | 23.1 |
| + blocks compiled as calls | 23.4 |
| + inline integer/load/store | 31.4 |
| + inline branches, EndBlock, block chaining | 42.6 |
| + register locals (no gain on this test) | 42.9 |
| + self-loops, tail calls | 58.5 (display-capped at 60) |

### Floating point / paired singles / SPRs (inline, `EmitInlineFP`)

Rule: an inline result must be bit-identical to the Interpreter. Anything that could differ
(NaN inputs/outputs, single-precision madd rounding ties, operands with excess precision) branches
to the Interpreter call at run time. FPRF/FI/FR are not updated (same as Jit64's defaults); `Rc=1`
forms go to the Interpreter.

- Loads/stores: `lfs(u)(x)`, `lfd(u)(x)`, `stfs(u)(x)`, `stfd(u)(x)`; `psq_l(u)`/`psq_st(u)` for the
  float GQR type (quantized integer types and HID2.LSQE=0 go to the Interpreter).
- Opcode 59 (`fadds`..`fnmadds`), opcode 63 (`fadd/fsub/fmul/fdiv`, `fsel`, `fcmpu/fcmpo`, `frsp`,
  `fctiwz`, `fmr/fneg/fabs/fnabs`), opcode 4 (`ps_*` arithmetic, merges, sums, muls/madds0/1,
  compares, moves).
- `mfspr`/`mtspr` for LR, CTR, XER (GetXER/SetXER split fields), GQR0-7, SPRG0-3 (supervisor ones
  check MSR.PR at run time and fall back to the Interpreter, which raises the exception).

Bisecting a miscompile: `globalThis.DOLPHIN_WASM_JIT_DISABLE = <bitmask>` turns groups back into
Interpreter calls: 1 FP loads/stores, 2 psq, 4 FP single, 8 FP double, 16 FP moves, 32 FP compares,
64 paired-single arithmetic, 128 integer.

`test-content/fpbench`: paired-single/FP kernel; prints a checksum (rows 300/330) that must be the
same with `DOLPHIN_WASM_JIT=0` and `2` (currently `c056 bd4f`). `scripts/readrow.py` reads it from
a screenshot.

| Test | JIT off | JIT on |
|---|---|---|
| cpubench | 23.1 fps | 56-58 fps (display-capped) |
| fpbench | 11.0 fps | 32.8 fps |

Bugs found this way: `psq_st` kept the second value in a local that address translation reused
(wrong ps1 stored; likely the black screen after Start in Mario Superstar Baseball).

Also inline: `stfiwx`, `subfze`, `fres`/`frsqrte`/`ps_res`/`ps_rsqrte` (table estimates through the function table; zero/NaN/inf/negative to the Interpreter), `lmw`/`stmw` (whole range in one page), `mfmsr`, CR logic ops (`crand`..`crxor`),
`mcrf`, `mfcr`, `mtcrf`, `psq_lx`/`stx`/`lux`/`stux`, and double `fmadd`/`fmsub`/`fnmadd`/`fnmsub`
(WebAssembly has no FMA, so these call `std::fma` through the function table; NaN results go to
the Interpreter). `DOLPHIN_WASM_JIT_DISABLE` group 256 = CR ops.

Next: `mfcr`/`mtcrf`/`mcrf`, CR logic ops, quantized integer psq types, `fres`/`frsqrte`,
FPRs cached in locals.

### WebGL depth range

WebGL rejects `glDepthRange(near > far)` (INVALID_OPERATION, the old range stays), and it has no
depth clamp. Games that use an inverted console depth range (`zRange < 0`, e.g. Mario Superstar
Baseball's cutscenes) then drew nothing: black screen with audio running. On Emscripten,
`VertexShaderManager::UseVertexDepthRange()` now maps depth in the vertex shader for those
viewports (glDepthRange stays 1-MAX..1). Without depth clamp, geometry outside the console's
depth range clips instead of clamping.

### WebGL readback (EFB copies to RAM, EFB peeks)

Emscripten's `glMapBufferRange` refuses `GL_MAP_READ_BIT`, and WebGL2 `glReadPixels` only takes
RGBA/UNSIGNED_BYTE (normalized) or RGBA/FLOAT (float buffers), not GL_BGRA or GL_RED. On
Emscripten, `OGLStagingTexture` keeps its data in CPU memory: readback does `glReadPixels` into it
(RGBA8, BGRA8 with an R/B swap, R32F via RGBA/FLOAT), uploads `glTexSubImage3D` from it.
Before this, every EFB-to-RAM copy logged `readPixels: invalid format` and returned nothing.

### WebGL feedback loops

WebGL rejects a draw when a texture attached to the bound framebuffer is also bound to a texture
unit the program samples from, even if the shader never reads it ("Feedback loop formed between
Framebuffer and active Texture"). Dolphin leaves stale bindings (e.g. a texture-cache entry it
later renders into). On Emscripten, `OGLGfx::Draw`/`DrawIndexed` call `BreakFeedbackLoops()`, which
unbinds such textures first. Seen as a black screen after the Mario Superstar Baseball main menu.

### Profiling a real game (2026-11-27, Mario Superstar Baseball, RTX 3060, Chrome 158)

Chrome Performance trace: the page's main thread (CPU emulation + Dolphin's GL calls) was busy
35%; Chrome's GPU process main thread was busy 96%. `retro_run` took ~8.5 ms but ran only ~12
times a second: the page waits for the GPU process to finish frames. So the bottleneck is the
cost of Dolphin's WebGL command stream in the GPU process (ANGLE/D3D11), not the PowerPC side.

Counters to find out which part: `Module.dolphinGLStats()` returns running totals (draws, buffer
uploads/KB, texture uploads/KB, shader compiles, program links, readbacks, EFB copies, texture
and program binds, flushes); the website overlay shows them per second.
`globalThis.DOLPHIN_GL_STREAM = 1` switches vertex/index/uniform uploads from glBufferSubData
into one buffer (WebGL has no base vertex, so Dolphin rewrites from offset 0 every flush) to
glBufferData orphaning, for A/B tests.

### WebGL dual-source blending

Without dual-source blending, Dolphin writes the 6-bit EFB alpha in ocol0.a, and blending uses
that value too: with a constant destination alpha (`bpmem.dstalpha`), draws blend with the
constant instead of the fragment's alpha, so things can vanish (Mario Superstar Baseball's menu
UI). Chrome has `WEBGL_blend_func_extended`: on Emscripten, OGLConfig enables it if a test
shader with `layout(location = 0, index = 1)` compiles, `FRAGMENT_OUTPUT_LOCATION_INDEXED`
becomes `layout(location = x, index = y)` (WebGL has no glBindFragDataLocationIndexed), and
`DOLPHIN_GL_DUALSRC=0` turns it off. `scripts/shadercheck.mjs` boots a test DOL with
ubershaders precompiled to compile every GX shader variant in headless Chrome: the ubershader
currently fails (`case label type does not match switch init-expression type`), with or
without dual-source; ubershaders are off by default.

### Depth comparisons (the missing-menu bug)

The first black-screen fix set `bSupportsReversedDepthRange = false` for Emscripten (WebGL rejects
`glDepthRange(near > far)`). With that flag VideoCommon stores depth as 1 - z, and the backend
must flip its depth comparisons (D3D's table: NEVER, GREATER, EQUAL, GEQUAL, LESS, NOTEQUAL,
LEQUAL, ALWAYS). OGL never did, so every depth test was backwards: UI drawn in front of a
background failed LEQUAL (Mario Superstar Baseball's menu). `OGLGfx::ApplyDepthState` now uses
the flipped table on Emscripten and `ClearRegion` clears to 1 - z. Found by replaying a FIFO log.

Also: depth clamping via `EXT_depth_clamp` (+ `WEBGL_clip_cull_distance` when present;
`DOLPHIN_GL_DEPTHCLAMP=0` to disable), so oversized viewport depth ranges clamp like on desktop.

### FIFO logs

Desktop Dolphin's Tools > FIFO Player records a few frames of GPU commands (.dff). The browser
build boots them (FifoPlayerThread doesn't enter its blocking loop in libretro single core; the
FIFO CPU core breaks after each frame). `scripts/fifoplay.mjs file.dff out.png` plays one in
headless Chrome. `DOLPHIN_GL_TRACE_FROM=N` (+ `DOLPHIN_GL_TRACE_COUNT`) logs draws, EFB copies
and clears with their BP/XF state to the console as `[gltrace]`.

### Profiling compiled blocks

Each compiled block's module carries a name section, so Chrome profiles show `ppc_<address>`
instead of `wasm-function[0]`. `EJS_emulator.Module._dolphin_disasm(0x80123456, 40)` prints the
PowerPC instructions there (console). TTYD (2026-11-27): main thread 97% busy, 73% in compiled
blocks: CPU-bound, unlike Mario Superstar Baseball (GPU-process-bound: draw-call count).

### Long blocks (conditional continue + branch following)

TTYD's hottest loop (0x8014505c, 16.5% of the time) was 5 blocks per iteration: every `beq` ended
a block, and block exits (flush, downcount, map lookup, call) cost more than 3-5 instructions.
With the WebAssembly JIT on, the analyzer now uses OPTION_CONDITIONAL_CONTINUE and
OPTION_BRANCH_FOLLOW like Jit64: conditional branches become `EndBlockIfTaken` (exit only if
npc != fall-through; inline in wasm, flushing registers but keeping them for the fall-through),
followed `b`/`bl` don't end the block, and the callee's `blr` is skipped by the analyzer.
fpbench: 34 -> 39 fps, 138 -> 65 blocks, same checksum. `DOLPHIN_WASM_JIT_LONGBLOCKS=0` reverts.

### GX constants upload (per-draw cost)

ProgramShaderCache::UploadConstants re-uploaded all GX constants (~6.6 KB: pixel + vertex +
geometry) through the stream buffer and re-bound 4 UBO ranges whenever anything changed: once
per draw, ~800 draws a frame in Mario Superstar Baseball gameplay, which saturated Chrome's GPU
process. On Emscripten the constants live in one persistent UBO that stays bound; only the dirty
sections (pixel / vertex / geometry) are uploaded with glBufferSubData. Utility uniforms (which
bind the stream buffer) mark it for re-binding; custom pixel constants use the old path.
Dirty sections are then compared with a shadow copy of the buffer and only changed spans
(16-byte grain, merged across gaps < 256 bytes) are uploaded. Menu FIFO: buffer uploads
130 -> 50 (sections) -> 9.5 KB per frame (diff), same picture. Real games: Mario Superstar
Baseball gameplay 41% -> ~60% speed, TTYD 34% -> 41% (sections only). `DOLPHIN_GL_UBO_PARTIAL=0`
reverts.

### WebGL call count

Bytes uploaded don't matter much in Chrome's GPU process; the number of calls does (cutting
constant uploads 3x in bytes didn't speed Baseball up). `scripts/glcalls.mjs` counts WebGL calls
per frame by name while replaying a FIFO log. Menu: ~12.5 calls per draw, ~2.5 of them redundant
`bindBuffer` and ~0.6 redundant `activeTexture`. A JS-side filter installed on GLctx at init
(`dolphin_webgl_install_call_filter`, `DOLPHIN_GL_FILTER=0` to disable) drops binds that don't
change anything (ELEMENT_ARRAY_BUFFER tracked per VAO; deletes reset the cache): 475 -> 374 calls
per frame, identical picture. The constants upload now does at most one bufferSubData per dirty
section (first to last changed vec4).

### Draw batching (DOLPHIN_GL_BATCH)

WebGL2 has no base-vertex draws, so the BufferSubData stream buffer rewrites offset 0 of the
vertex and index buffers before every draw: 2 uploads per draw. The call filter
(`dolphin_webgl_install_call_filter` in Common/GL/GLUtil.cpp, now a wrapper over every GLctx
method) can record instead: after each draw (`WebGLBatchBegin` in OGLGfx::Draw/DrawIndexed) the
following calls are queued. Stream-buffer uploads go into JS staging arrays; each draw is
rewritten to its staged data (indices rebased on the CPU by vertex offset / VAO stride and widened
to u32; primitive restart 0xFFFF -> 0xFFFFFFFF). A flush uploads the staged vertices and indices in
one bufferSubData each, then replays the queue. Any call not on the record list (getParameter
except binding queries, which are answered from tracked state; texture uploads; deletes;
readbacks) flushes first, and GLContextLR::Swap and the end of retro_run always flush. Draws that
reuse data uploaded before the run (second passes) re-stage a JS copy of the last upload.
Menu FIFO: 374 -> 244 calls per frame, ~10 draws per flush, no draws falling back.
`gl.__dolphinBatchStats` (on the WebGL2 context) counts flushes, recorded draws and what forced
flushes. The same layer now also drops redundant enable/disable, blend/depth/scissor/viewport,
useProgram, bindTexture/bindSampler and uniform-buffer range rebinds. `DOLPHIN_GL_BATCH=0` /
`DOLPHIN_GL_FILTER=0` turn the parts off.

Remaining per-draw calls: one constants upload (bufferSubData) per changed section, texture binds,
blend state. Versioning the constants buffer would only trade bufferSubData for bindBufferRange.

## WebGPU backend (branch `webgpu`, experimental)

`Source/Core/VideoBackends/WebGPU`: a new Dolphin video backend for the browser build, on with
`DOLPHIN_WEBGPU=1` (jl-emu-globals). RetroArch still gets a WebGL context; the core renders with
WebGPU and copies each finished frame into RetroArch's GL framebuffer.

- **Split C++/JS.** `WGPUBackend.cpp` implements Dolphin's abstract renderer (textures, staging
  textures, framebuffers, shaders, pipelines, vertex formats, Gfx, VertexManager) as thin handles;
  `WGPURuntime.js` does the WebGPU work through small EM_JS stubs (no webgpu.h, so no Emscripten
  upgrade). `scripts/gen_wgpu_embed.py` embeds the runtime and naga into `WGPUEmbedded.inc`
  (generated, not in the patch; build.sh runs it).
- **Device.** Requested at module start (`AutoKick` static initializer) because it's asynchronous;
  by the time a game boots it's ready. Optional features used if present: dual-source-blending,
  float32-filterable, texture-compression-bc, depth32float-stencil8.
- **Shaders.** Dolphin generates Metal-style GLSL 4.5 (api_type Metal: same NDC/depth/origin
  conventions as WebGPU). glslang preprocesses it (`SPIRV::PreprocessShader`), then JS rewrites
  what WGSL can't express and naga (npm `naga-wasm`, 2 MB wasm, embedded) translates to WGSL:
  combined image-samplers split into texture (binding 2n) + sampler (2n+1), including function
  parameters; `bool` uniform-block members become uint; `isnan()` expanded; a lone `index = 0`
  output loses its index. WGSL gets `diagnostic(off, derivative_uniformity)` because Dolphin samples
  inside non-uniform branches. Dynamic sampler indexing, geometry/compute shaders, palette
  conversion, logic ops, bbox, depth clamp, large points are reported unsupported.
- **Binding.** Group 0: one uniform ring buffer bound three times with dynamic offsets (PS, VS, GS;
  utility pipelines read the utility block at all three). Group 1: textures/samplers, layout taken
  from the WGSL's declared bindings; bind groups cached per pipeline. A texture that's an attachment
  of the current pass is replaced with a dummy (the EFB stays bound to unused samplers). Depth
  textures are sampled through an r32float copy made with copyTextureToBuffer.
- **Passes.** Begun lazily on the first draw after SetFramebuffer; whole-target clears become the
  pass's loadOp (as Metal); copies and uploads end the pass. Uploads go through mapped staging
  buffers inside the command encoder so they stay ordered with draws. Destroyed objects are kept
  until the next submit.
- **Batched uploads.** Vertex, index and uniform data is copied into JS mirrors of the GPU ring
  buffers and written with one writeBuffer per buffer just before each submit (menu FIFO: 3 writes
  per frame instead of one per draw per buffer). Texture uploads go through queue.writeTexture,
  which doesn't end the render pass, unless the texture is already referenced by commands recorded
  since the last submit (tracked per texture); then they're copied inside the command stream.
- **Readbacks** are asynchronous: StagingTexture::Map returns whatever has arrived (usually the
  previous frame's data) instead of stalling.
- **Present.** Default: copy the backbuffer to a mappable buffer, texImage2D it into a GL texture
  when the map completes, blit into RetroArch's framebuffer (a frame or two of latency).
  `DOLPHIN_WEBGPU_PRESENT=1` tries the GPU-only route (copy to a WebGPU canvas, texImage2D from
  the canvas); in headless SwiftShader the canvas snapshot came back blank, untested on real GPUs.
- **Testing.** Headless Chrome runs WebGPU on SwiftShader (`--enable-unsafe-webgpu
  --use-webgpu-adapter=swiftshader --enable-features=Vulkan`), but WebGPU canvases don't show in
  screenshots: read textures back with `Module.__wgpu._list()` / `_read(id)` (scripts/tex2png.py).
  `Module.__wgpu.stats` counts draws, passes, submits, shader failures. `scripts/xlat.mjs` runs the
  GLSL->WGSL path in node on dumped shaders. Menu FIFO: all 26 shaders translate, no validation
  errors, picture matches the WebGL build.
- **Known gaps.** Viewports are clamped to the target (WebGPU requires it), which distorts
  geometry when a game's viewport extends off-screen; no MSAA; R16 textures map to r16float;
  D24S8 depth can't be read back or sampled; ~7 draws per render pass on the menu (uploads and
  copies end passes).

### Gather pipe stores in the JIT

Baseball trace on the WebGPU build (2026-11-28): main thread ~96% busy; ~71% CPU emulation
(37.5% compiled PowerPC code, 15.4% Interpreter fallbacks, 12.3% the CachedInterpreter dispatch
loop), ~22% GPU side (10.8% WebGPU JS calls). The fallbacks were mostly `stw`/`stb` (and FP
stores) to 0xCC008000: games feed the GPU by storing to the write-gather pipe, which isn't RAM, so
those stores took the Interpreter -> MMU::WriteToHardware -> GPFifo path. Compiled stores now
check for the gather pipe page first (EA 0xCC008000 with MSR.DR, 0x0C008000 without; non-update
forms): the value is written big-endian at `ppcState.gather_pipe_ptr` (what GPFifoManager::FastWrite*
does), and `FastCheckGatherPipe` is called once the pipe holds 32 bytes. Covers stb/sth/stw(x),
stfs/stfd/stfiwx and float psq_st (quantized psq_st still falls back). fpbench checksum unchanged.
`relink.sh` now links with `--profiling-funcs` so browser traces show function names.

### Pointers above 2 GB in JS glue

The heap is ~2.3 GB, so wasm pointers can be >= 0x80000000. Passed to EM_JS as `int`/pointer
they arrive in JS as negative numbers, and `HEAPU8.slice(p, ...)` / `subarray` then count from
the end of memory. Symptom (2026-11-28, WebGPU build, which allocates more): JIT blocks failed
with "expected magic 00 61 73 6d, found 00 00 00 00" and stayed interpreted (team select 30%,
gameplay ~10%), and WebGPU texture/vertex uploads read the wrong memory (garbage overlay in
gameplay). Every EM_JS that takes a pointer now does `p >>> 0` (JIT install, all wgpu_* stubs,
UTF8ToString calls, GL stats).

### RAM fast path for compiled loads/stores

Trace after the 2 GB fix (baseball gameplay, steady state): compiled PowerPC code 61-65% of the
main thread, GPU side 21-26%, dispatcher 3-4%, Interpreter ~0 (the 12% dispatcher in earlier
traces was warm-up). `Emitter::Translate` now starts with a fast path when, at compile time, the
DBAT table maps 0x80000000 + [0, RAM size) to physical RAM, cached, and data cache emulation is
off (`fast_ram`): `ea - 0x80000000 <= ram_size - size` -> host = ram + offset, skipping the BAT
table load, the dcache flag load, the page-crossing check and the RAM-range masking. Anything else
falls through to the old path. Safe because MMU::DBATUpdated clears the JIT cache. fpbench
checksum unchanged.

### Forward branches inside a compiled block

TTYD gameplay trace (2026-11-28): ~24% of the main thread in five 6-instruction blocks at
0x8014505c..0x801450c8 - one loop (64 iterations x 4 checks of a flag bit, collecting active
entries) where each `beq` skips 3 instructions. A taken conditional branch left the block
(EndBlockIfTaken), so every iteration crossed up to 5 block boundaries. Now, when the target of a
forward `bc` (no link) is a later instruction of the same block reached in straight-line order
(no followed branches or block ends in between) and properly nested inside any open region, the
taken path is a `br_if` out of a wasm `block` that ends right before the target. Registers:
dirty ones are flushed (kept dirty) before the branch, so memory is right on the taken path; at
the join only registers loaded on both paths stay loaded, and ones first loaded and modified
inside are written back at the end of the fall-through path. Skipped instructions' cycles still
count against the downcount (slight over-count). `DOLPHIN_WASM_JIT_FWDBRANCH=0` disables it.
Test: `test-content/brtest` (a copy of the TTYD loop plus nested forward branches): same checksum
with and without, loop 35.4 -> 43.9 fps headless; fpbench checksum unchanged.

### Dual core (experimental, `DOLPHIN_DUALCORE=1`)

Single core was forced because dual core blocks the main thread waiting for worker threads while
workers' filesystem calls are proxied to the main thread. With the flag, Boot.cpp leaves
MAIN_CPU_THREAD on: the CPU runs in a pthread (web worker) and retro_run does the libretro dual
core frame (DoFrameStep, then RunGpuLoop on the main thread until the CPU thread's StopGpuLoop),
so the GPU side (FIFO decode, vertex loading, WebGPU/WebGL) overlaps the CPU within each frame.
Emscripten's main-thread waits spin while processing proxied calls, so the boot-time file reads
from the CPU thread went through. Fix needed: audio (SYNC_PER_FRAME) is pushed from the CPU
thread, but RetroArch's audio driver (RWebAudio*) is JS bound to the main thread ("Cannot read
properties of null (reading 'sampleRate')" in the worker) - samples produced off the main thread
are queued and handed to batch_cb at the end of retro_run (`Audio::DrainPendingAudio`).
That crackled: a frame's samples arriving just after the hand-off waited a frame (bursts of two,
then none). Now in dual core the CPU thread's NewField skips the push and retro_run mixes one
frame's worth (`Stream::PushAudioForFrame`) on the main thread after each frame.
The JIT compiles and runs blocks on the CPU thread (its own function table), so
`Module.wasmJitBlocks` / failures are counted on the worker's Module: the overlay's `jit` line
reads 0 in dual core. Headless: fpbench and brtest checksums match single core (WebGL and WebGPU).
Each thread has its own wasm function table: the first real dual-core boot died with
"WebAssembly.Table.set(): invalid" when a block compiled on the CPU thread was destroyed from the
main thread. `dolphin_wasm_jit_install` now records the slots it fills (`Module.__wasmJitOwn`, per
thread) and `dolphin_wasm_jit_free` only frees those; a slot owned by another thread is left
allocated there (the block's entry.fn is cleared, so it's never called).
First real test (baseball, actually still single core - the flag wasn't set): stadium load 82-84% (vs ~60%), gameplay unchanged at 50-60% - the
threads don't overlap much in gameplay yet; needs a dual-core trace (main vs worker timelines).

## Known issues / TODO (rough priority)

1. **Test real games** (user-supplied). Watch the console for `[libretro ERROR]` and aborts.
2. **Performance**: keep extending the WebAssembly JIT (see its Next list). Heap is 2 GB even for tiny programs; check.
3. Audio: confirm DSP HLE audio actually plays (RWebAudio path); tune `audio_latency`.
4. Wii: needs Wiimote mapping defaults in EmulatorJS and NAND/SYSCONF handling; untested.
5. Large ISOs: EmulatorJS loads the whole game into MEMFS (RAM). Prefer RVZ/compressed
   formats; longer term, a lazy File-backed FS node (read-on-demand) like the Dolphin-for-web
   port at bit-axis-labs does.
6. Save data persistence (`/data/saves` is IDBFS; check memory cards/Wii saves land there).
7. EmulatorJS default control mapping for `gamecube` / `wii`, and a `cores/reports/dolphin.json`
   (enables core caching; currently a 404 warning).
8. Consider shader pre-compile / ubershaders option for stutter.

## Toolchain without emsdk

The build VM couldn't download emsdk's prebuilt binaries, so the toolchain was assembled by hand:
system LLVM 18 (`apt install llvm-18 lld-18 clang`), Emscripten **3.1.52** from git (last
release expecting LLVM 18; run `./bootstrap`, set `LLVM_ROOT`/`BINARYEN_ROOT`/`NODE_JS` in
`.emscripten`), Binaryen `version_115` built from source. Emscripten ports that download
tarballs (zlib) were satisfied by placing a source tree in `cache/ports/zlib/zlib-1.2.13`
with a matching `.emscripten_url` marker. Dolphin's bzip2 submodule (gitlab) was replaced with
the Ubuntu bzip2 1.0.8 source. On a normal machine `scripts/setup.sh` (emsdk) avoids all of this.

## Test program

`test-content/colorbars`: bare-metal C + asm, built with
`clang --target=powerpc-unknown-eabi -mcpu=750`, linked at 0x80003100, converted with
`elf2dol.py`. Sets VI for 480i and fills the XFB with moving bars. Its VI/YUV setup is
approximate (bars are not perfectly placed) — it exists only to prove CPU + VI + XFB work.

### Dual core: page freeze around save states (fixed)

The site autosaves every 30 s. A save pauses the CPU and the GPU loop's
"emulator running" flag; on the next `retro_run` the main thread entered
`RunGpuLoop()` and returned early every pass ("do nothing while paused")
while waiting for the CPU thread's end-of-frame `StopGpuLoop`, which never
came. The browser's main thread was stuck, so the tab couldn't even reload.
Fix: on emscripten the GPU loop now ends as soon as the CPU thread is parked
(stepping and idle), checked before the paused early return; after each
frame `RunGpuLoopUntilCPUIdle(50)` services any GPU work the CPU queued
between its frame-end signal and parking. Headless test: 30 back-to-back
saves in dual core with no hang; fpbench/brtest checksums unchanged.
