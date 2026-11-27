#!/usr/bin/env bash
# Fetch toolchain + sources at the pinned revisions this port was developed against.
# Usage: scripts/setup.sh [WORK_DIR]   (default: ./work)
# Requires: git, python3, cmake, ninja, make, 7z (p7zip-full), zip, node >= 18
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-$REPO_DIR/work}"
mkdir -p "$WORK" && cd "$WORK"

EMSDK_VERSION=3.1.52
DOLPHIN_REV=a91b1d311d4d57757282e0644a63bfd100d8d232      # libretro/dolphin
RETROARCH_REV=97f2cfa65d01d57406e4ce9459242c8ada7c7e3b    # EmulatorJS/RetroArch
EMULATORJS_REV=5a65bde5d3b47c75f22e90453c3c14c469411589   # EmulatorJS/EmulatorJS

clone_at() { # url dir rev
  if [ ! -d "$2/.git" ]; then git clone "$1" "$2"; fi
  git -C "$2" fetch --depth 1 origin "$3" 2>/dev/null || git -C "$2" fetch origin
  git -C "$2" checkout -q "$3"
}

# --- Emscripten (standard emsdk route) ---
if [ ! -d emsdk ]; then git clone --depth 1 https://github.com/emscripten-core/emsdk.git; fi
(cd emsdk && ./emsdk install "$EMSDK_VERSION" && ./emsdk activate "$EMSDK_VERSION")
# If emsdk downloads are blocked on your network, see NOTES.md "Toolchain without emsdk".

# --- Sources ---
clone_at https://github.com/libretro/dolphin.git dolphin "$DOLPHIN_REV"
git -C dolphin submodule update --init --recursive --jobs 4 || true
clone_at https://github.com/EmulatorJS/RetroArch.git RetroArch "$RETROARCH_REV"
clone_at https://github.com/EmulatorJS/EmulatorJS.git EmulatorJS "$EMULATORJS_REV"

# --- Patches ---
apply() { # dir patch
  if git -C "$1" apply --check "$2" 2>/dev/null; then git -C "$1" apply "$2"; echo "applied $(basename "$2")";
  else echo "skip $(basename "$2") (already applied?)"; fi
}
apply dolphin "$REPO_DIR/patches/dolphin.patch"
apply dolphin/Externals/SFML/SFML "$REPO_DIR/patches/dolphin-sfml.patch"
apply RetroArch "$REPO_DIR/patches/retroarch-emulatorjs.patch"
apply EmulatorJS "$REPO_DIR/patches/emulatorjs.patch"

echo
echo "Setup done in $WORK. Next: scripts/build.sh $WORK"
