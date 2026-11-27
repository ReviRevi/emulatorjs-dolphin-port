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

Next: floating point / paired singles (all Interpreter fallbacks today; most game code),
`mtspr`/`mfspr`, `lmw`/`stmw`, CR logic ops, `fres`/`frsqrte` tables. Match Jit64's FP behaviour
(no full FPSCR exception tracking), not the Interpreter's.

## Known issues / TODO (rough priority)

1. **Test real games** (user-supplied). Watch the console for `[libretro ERROR]` and aborts.
2. **Performance**: extend the WebAssembly JIT (see above) to floating point / paired singles.
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
