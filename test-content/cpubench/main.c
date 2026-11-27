// GameCube CPU correctness test + benchmark for the WebAssembly JIT.
// Background: grey while checking, green = the first 32 results all matched
// the native build, red = a mismatch. Below: one tick per
// iteration, so the speed of the emulated CPU shows as the speed of the ticks.
#include "work.h"
#include "expected.h"
#define VI16(o) (*(volatile u16*)(0xCC002000 + (o)))
#define VI32(o) (*(volatile u32*)(0xCC002000 + (o)))
#define XFB_PHYS 0x00300000u
#define XFB ((volatile u32*)(0xC0000000u | XFB_PHYS))
#define W 640
#define H 480
#define BLACK 0x10801080u
#define WHITE 0xEB80EB80u
#define GREEN 0x91229136u
#define RED   0x51F0515Au
#define GREY  0x7E807E80u

static void fill(u32 x0, u32 y0, u32 w, u32 h, u32 c) {
  for (u32 y = y0; y < y0 + h; y++) for (u32 x = x0 / 2; x < (x0 + w) / 2; x++) XFB[y * (W / 2) + x] = c;
}

void main_c(void) {
  VI16(0x00) = (240 << 4) | 6;
  VI32(0x0C) = (3u << 16) | 0x18;
  VI32(0x10) = (2u << 16) | 0x19;
  VI16(0x48) = (40 << 8) | 80;
  VI32(0x1C) = (1u << 28) | (XFB_PHYS >> 5);
  VI32(0x24) = (1u << 28) | ((XFB_PHYS + W * 2) >> 5);
  VI16(0x02) = 0x0001;
  u32 ok = 1, bg = GREY;
  fill(0, 0, W, H, bg);
  for (u32 i = 0;; i++) {
    u32 r = work(i);
    if (i < 32) {
      if (r != expected[i]) ok = 0;
      u32 nbg = ok ? (i == 31 ? GREEN : GREY) : RED;
      if (nbg != bg) { bg = nbg; fill(0, 0, W, H, bg); }
    }
    // progress: ticks across rows 120..460, 8 px per iteration
    u32 slot = i % (80 * 14);
    u32 row = 120 + (slot / 80) * 20, col = (slot % 80) * 8;
    if (slot == 0) fill(0, 120, W, 280, bg);
    fill(col, row, 6, 14, WHITE);
    // iteration count in binary (16 bits, LSB first) on rows 400..430
    for (u32 b = 0; b < 16; b++) fill(8 + b * 26, 400, 20, 30, ((i + 1) >> b) & 1 ? WHITE : BLACK);  // LSB first
  }
}
