# emulatorjs-dolphin-port

A GameCube / Wii **RetroArch (libretro) core for the browser**, built from
[libretro/dolphin](https://github.com/libretro/dolphin) and packaged so it plugs into
[EmulatorJS](https://github.com/EmulatorJS/EmulatorJS) like any other core.

> **Status: experimental proof of concept.** The core loads in EmulatorJS, boots
> GameCube programs, and renders through Dolphin's own hardware renderer on WebGL2.
> The CPU runs on a new **PowerPC → WebAssembly JIT** (integer, memory and branch
> instructions so far; floating point still interpreted), so commercial games are
> still below full speed.

## Try it

`dist/` is a self-contained test site: patched EmulatorJS + the prebuilt core +
a homebrew test program (`colorbars.dol`).

```bash
cd dist
python3 serve.py 8080        # sends the COOP/COEP headers threads need
# open http://localhost:8080/
# your own game: http://localhost:8080/?game=/path/under/dist/MyGame.rvz
```

Threaded EmulatorJS cores need `SharedArrayBuffer`, so the page **must** be served with:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

For nginx:

```nginx
add_header Cross-Origin-Opener-Policy same-origin always;
add_header Cross-Origin-Embedder-Policy require-corp always;
```

### Using it on an existing EmulatorJS site

1. Apply `patches/emulatorjs.patch` to your EmulatorJS checkout (or copy
   `dist/data/src/{consts.js,emulator.js,GameManager.js}`). It adds the `gamecube` / `wii`
   systems, marks the core as needing threads + WebGL2, and loads Dolphin's Sys files.
2. Copy `dist/data/cores/dolphin-thread-wasm.data` and `dist/data/cores/dolphin-sys.zip`
   into your `data/cores/`.
3. On the page: `EJS_core = "gamecube"; EJS_threads = true;`

## Build from source

```bash
scripts/setup.sh work     # emsdk 3.1.52 + pinned Dolphin / RetroArch / EmulatorJS + patches
scripts/build.sh work     # -> work/EmulatorJS/data/cores/dolphin-thread-wasm.data
```

A full build takes a while (Dolphin is ~1,200 compile units). See `NOTES.md` for how the
port works, what was changed, and what's next.

## Layout

| Path | What |
|---|---|
| `patches/dolphin.patch` | Emscripten support for libretro/dolphin |
| `patches/dolphin-sfml.patch` | Lets bundled SFML compile under Emscripten |
| `patches/retroarch-emulatorjs.patch` | Registers `dolphin` in EmulatorJS's RetroArch build script |
| `patches/emulatorjs.patch` | EmulatorJS frontend support (systems, threads/WebGL2, Sys files) |
| `packaging/core.json` | EmulatorJS core metadata (extensions, default options) |
| `scripts/` | setup / build / local server / headless browser smoke test |
| `test-content/colorbars/` | Tiny bare-metal GameCube test program (clang PowerPC) |
| `test-content/cpubench/` | Integer CPU benchmark (green = correct, binary counter = iterations) |
| `test-content/fpbench/` | FP / paired-single benchmark; checksum must match with the JIT on and off |
| `dist/` | Ready-to-serve test site with prebuilt core |

## License

Dolphin is GPL-2.0-or-later, EmulatorJS and RetroArch are GPL-3.0. The combined core and
this repository's patches/scripts are distributed under **GPL-3.0** (see `LICENSE`).
You must supply your own legally-dumped games.
