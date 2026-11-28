typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#define VI16(o) (*(volatile u16*)(0xCC002000 + (o)))
#define VI32(o) (*(volatile u32*)(0xCC002000 + (o)))
#define XFB_PHYS 0x00300000u
#define XFB ((volatile u32*)(0xC0000000u | XFB_PHYS))
#define W 640
#define H 480
static void fill(u32 x0, u32 y0, u32 w, u32 h, u32 c) {
  for (u32 y = y0; y < y0 + h; y++) for (u32 x = x0 / 2; x < (x0 + w) / 2; x++) XFB[y * (W / 2) + x] = c;
}
static void bits(u32 row, u32 v) { for (u32 b = 0; b < 16; b++) fill(8 + b * 26, row, 20, 24, (v >> b) & 1 ? 0xEB80EB80u : 0x10801080u); }
u32 scan_active(u8* out, const u8* table);
u32 nested(u32 a, u32 b);
static u8 table[64 * 608 + 0x300];
static u8 out[300];
static u32 seed = 7;
static u32 rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static u32 checksum = 0;
static void mix(u32 v) { checksum = (checksum ^ v) * 16777619u; }
void main_c(void) {
  VI16(0x00) = (240 << 4) | 6; VI32(0x0C) = (3u << 16) | 0x18; VI32(0x10) = (2u << 16) | 0x19;
  VI16(0x48) = (40 << 8) | 80; VI32(0x1C) = (1u << 28) | (XFB_PHYS >> 5);
  VI32(0x24) = (1u << 28) | ((XFB_PHYS + W * 2) >> 5); VI16(0x02) = 0x0001;
  fill(0, 0, W, H, 0x51F0515Au);
  for (u32 pass = 0; pass < 200; pass++) {
    for (u32 i = 0; i < 64 * 4; i++) {
      u32 off = (i / 4) * 608 + 0x90 + (i % 4) * 0x98;
      u16 v = (rnd() & 3) == 0 ? 0x8000 : 0x1234;
      table[off] = v >> 8; table[off + 1] = v & 0xff;
    }
    u32 n = scan_active(out, table);
    mix(n);
    for (u32 i = 0; i < n; i++) mix(out[i]);
    for (u32 a = 0; a < 3; a++) for (u32 b = 0; b < 3; b++) mix(nested(a, b));
  }
  bits(300, checksum & 0xffff);
  bits(330, checksum >> 16);
  for (u32 it = 0;; it++) { mix(scan_active(out, table)); bits(380, it); }
}
