// Shared by the native build (expected results) and the GameCube build.
// Only well-defined C: no signed overflow, no shifts >= 32, no division by 0.
typedef unsigned int u32; typedef int s32; typedef unsigned short u16; typedef unsigned char u8;
typedef signed short s16; typedef signed char s8;

static u32 table[256];
static u8 bytes[512];

static inline u32 rotl(u32 v, u32 s) { s &= 31; return s ? (v << s) | (v >> (32 - s)) : v; }

static u32 work(u32 seed) {
  u32 a = seed * 2654435761u + 1, b = seed ^ 0x9e3779b9u, c = 7, d = 0;
  for (u32 i = 0; i < 256; i++) table[i] = a = a * 1103515245u + 12345u;
  for (u32 i = 0; i < 512; i++) bytes[i] = (u8)(table[i & 255] >> (i & 7));
  for (u32 i = 0; i < 2000; i++) {
    a = a * 1664525u + 1013904223u;
    b ^= rotl(a, i) + (b >> 3);
    u32 idx = (a >> 24) & 255;
    table[idx] += b;
    c += table[(idx + 17) & 255] >> (i & 15);
    s32 sa = (s32)(a >> 1), sb = (s32)((b | 1) & 0x7fff) - 0x4000;
    if (sb == 0) sb = 3;
    d += (u32)(sa / sb) + (u32)(sa % sb);
    d ^= (b / ((c & 0xffff) | 1));
    s16 h = (s16)(a >> 9);
    s8 by = (s8)bytes[(a ^ i) & 511];
    c += (u32)(s32)h + (u32)(s32)by;
    bytes[(b ^ i) & 511] = (u8)(c >> 5);
    if ((s32)c < (s32)d) c = c - d * 3; else if (c > (d ^ 0x55aa55aau)) d = d + (c >> 2);
    u32 z = a & 0xfffff;
    u32 lz = 0; while (z && !(z & 0x80000000u)) { z <<= 1; lz++; }
    d += lz;
    c ^= (u32)((unsigned long long)a * b >> 32);
  }
  return a ^ b ^ c ^ d;
}
