#!/usr/bin/env bash
# Build the Dolphin libretro core for EmulatorJS (threads + WebGL2).
# Usage: scripts/build.sh [WORK_DIR]
# Output: $WORK/EmulatorJS/data/cores/dolphin-thread-wasm.data and dolphin-sys.zip
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-$REPO_DIR/work}"
JOBS="${JOBS:-$(nproc)}"

if [ -z "${EMSCRIPTEN:-}" ]; then
  # shellcheck disable=SC1091
  source "$WORK/emsdk/emsdk_env.sh" >/dev/null
fi

# 0. WebGPU backend: embed its JS runtime and the naga shader translator (npm naga-wasm)
NAGA_DIR="$WORK/naga-wasm"
if [ ! -f "$NAGA_DIR/package/dist/wasm/naga_bg.wasm" ]; then
  mkdir -p "$NAGA_DIR" && (cd "$NAGA_DIR" && npm pack naga-wasm@30.2.0 >/dev/null && tar xzf naga-wasm-*.tgz)
fi
python3 "$REPO_DIR/scripts/gen_wgpu_embed.py" "$WORK/dolphin" "$NAGA_DIR/package"

# 1. Dolphin -> static wasm libraries
cd "$WORK/dolphin"
mkdir -p build-wasm && cd build-wasm
if [ ! -f build.ninja ]; then
  emcmake cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DLIBRETRO=ON -DENABLE_GENERIC=ON -DENABLE_HEADLESS=ON \
    -DENABLE_VULKAN=OFF -DENABLE_X11=OFF -DENABLE_EGL=OFF -DENABLE_HWDB=OFF \
    -DUSE_MGBA=OFF -DENABLE_LTO=OFF -DUSE_SYSTEM_LIBS=OFF -DWITH_OPTIM=OFF \
    -DCMAKE_C_FLAGS="-pthread" -DCMAKE_CXX_FLAGS="-pthread"
fi
ninja -j"$JOBS" dolphin_libretro

# 2. Merge every static lib into one archive (what EmulatorJS's RetroArch links)
{
  echo "create dolphin_libretro_emscripten.a"
  find . -name '*.a' -not -path './CMakeFiles/*' -not -name 'dolphin_libretro_emscripten.a' | sort | sed 's|^\./|addlib |'
  echo save; echo end
} > merge.mri
rm -f dolphin_libretro_emscripten.a
emar -M < merge.mri
cp dolphin_libretro_emscripten.a "$WORK/RetroArch/emulatorjs/dolphin_libretro_emscripten.bc"

# 3. Link with EmulatorJS's RetroArch frontend
cd "$WORK/RetroArch/emulatorjs"
rm -f ../../EmulatorJS/data/cores/dolphin-thread-wasm.data
emmake ./build-emulatorjs.sh --threads

# 4. Add EmulatorJS metadata to the core archive
PKG="$(mktemp -d)"
cp "$REPO_DIR/packaging/core.json" "$PKG/core.json"
cp "$WORK/dolphin/COPYING" "$PKG/license.txt"
printf '{\n    "minimumEJSVersion": "4.3.0",\n    "version": "2.0.3"\n}\n' > "$PKG/build.json"
(cd "$PKG" && 7z a -t7z "$WORK/EmulatorJS/data/cores/dolphin-thread-wasm.data" core.json license.txt build.json >/dev/null)
rm -rf "$PKG"

# 5. Dolphin's Sys folder (fonts, per-game settings) -> dolphin-sys.zip
rm -f "$WORK/EmulatorJS/data/cores/dolphin-sys.zip"
(cd "$WORK/dolphin/Data" && zip -qr9 "$WORK/EmulatorJS/data/cores/dolphin-sys.zip" Sys \
   -x "Sys/Themes/*" "Sys/wiitdb-*.txt")

# 6. Minified EmulatorJS (emulator.min.js/.css, so loader.js doesn't 404 and
#    fall back to the unbundled sources) and a core report (its buildStart
#    lets browsers cache the core until it changes).
(cd "$WORK/EmulatorJS" && npm i --no-audit --no-fund --no-save rollup @rollup/plugin-terser \
   @node-minify/core @node-minify/clean-css >/dev/null && npm run minify)
mkdir -p "$WORK/EmulatorJS/data/cores/reports"
HASH=$(cat "$WORK/EmulatorJS/data/cores/dolphin-thread-wasm.data" "$WORK/EmulatorJS/data/cores/dolphin-sys.zip" | sha1sum | cut -c1-12)
echo "{\"buildStart\": $((16#$HASH)), \"options\": {\"defaultWebGL2\": true}}" \
  > "$WORK/EmulatorJS/data/cores/reports/dolphin.json"

ls -la "$WORK/EmulatorJS/data/cores/" | grep dolphin
echo "Done. Serve $WORK/EmulatorJS (or copy data/ into dist/) with COOP/COEP headers."
