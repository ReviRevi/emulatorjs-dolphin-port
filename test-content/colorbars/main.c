// Minimal bare-metal GameCube test program: sets up VI for NTSC 480i,
// fills the external framebuffer with animated colour bars.
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#define VI16(o) (*(volatile u16*)(0xCC002000 + (o)))
#define VI32(o) (*(volatile u32*)(0xCC002000 + (o)))
#define XFB_PHYS 0x00300000u
#define XFB ((volatile u32*)(0xC0000000u | XFB_PHYS))  /* uncached */
#define W 640
#define H 480

/* YUY2 pairs: Y0 U Y1 V for 8 classic colour bars */
static const u32 bars[8] = {
  0xEB80EB80, /* white  */ 0xD292D210, /* yellow */ 0xAA10AAA6, /* cyan  */
  0x91229136, /* green  */ 0x6ADE6ACA, /* magenta*/ 0x51F0515A, /* red   */
  0x296E29F0, /* blue   */ 0x10801080  /* black  */
};

void main_c(void) {
  VI16(0x00) = (240 << 4) | 6;            /* VTR: ACV=240 lines/field, EQU=6 */
  VI32(0x0C) = (3u << 16) | 0x18;         /* VTO */
  VI32(0x10) = (2u << 16) | 0x19;         /* VTE */
  VI16(0x48) = (40 << 8) | 80;            /* PCR: WPL=40, STD=80 (interlaced) */
  VI32(0x1C) = (1u << 28) | (XFB_PHYS >> 5);              /* top field */
  VI32(0x24) = (1u << 28) | ((XFB_PHYS + W * 2) >> 5);    /* bottom field */
  VI16(0x02) = 0x0001;                    /* DCR: enable, interlaced, NTSC */

  u32 frame = 0;
  for (;;) {
    for (u32 y = 0; y < H; y++) {
      volatile u32* row = XFB + y * (W / 2);
      for (u32 x = 0; x < W / 2; x++) {
        u32 bar = ((x * 2 + frame * 4) / (W / 8)) & 7;
        row[x] = (y > 400 && ((x + frame) & 32)) ? 0x10801080 : bars[bar];
      }
    }
    frame++;
  }
}
