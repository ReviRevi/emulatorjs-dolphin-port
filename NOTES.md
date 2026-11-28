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
Menu FIFO: 130 -> 50 KB of buffer uploads per frame, same picture. `DOLPHIN_GL_UBO_PARTIAL=0`
reverts.

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
