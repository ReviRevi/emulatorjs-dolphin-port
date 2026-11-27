// GameCube floating-point / paired-single test + benchmark for the WebAssembly JIT.
// Computes a checksum over scalar float/double code (compiled by clang) and a
// hand-encoded paired-single kernel (ps_kernel.S), 64 passes so the JIT kicks
// in. The checksum is shown in binary (two rows of 16 squares, LSB first);
// compare it between JIT off (DOLPHIN_WASM_JIT=0) and on. Then loops forever
// (iteration counter on the third row) as a benchmark.
typedef unsigned int u32; typedef int s32; typedef unsigned short u16; typedef unsigned long long u64;
#define VI16(o) (*(volatile u16*)(0xCC002000 + (o)))
#define VI32(o) (*(volatile u32*)(0xCC002000 + (o)))
#define XFB_PHYS 0x00300000u
#define XFB ((volatile u32*)(0xC0000000u | XFB_PHYS))
#define W 640
#define H 480
#define BLACK 0x10801080u
#define WHITE 0xEB80EB80u
#define BLUE  0x296E29F0u

static void fill(u32 x0, u32 y0, u32 w, u32 h, u32 c) {
  for (u32 y = y0; y < y0 + h; y++) for (u32 x = x0 / 2; x < (x0 + w) / 2; x++) XFB[y * (W / 2) + x] = c;
}
static void bits(u32 row, u32 v) {
  for (u32 b = 0; b < 16; b++) fill(8 + b * 26, row, 20, 24, (v >> b) & 1 ? WHITE : BLACK);
}
static inline u32 fbits(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
static inline u64 dbits(double d) { union { double d; u64 u; } x; x.d = d; return x.u; }

void ps_kernel(const float* in, float* out, u32 n);

static float fin[8 * 64];
static float fout[46 * 64];
static u32 seed = 12345;
static u32 rnd(void) { seed = seed * 1103515245u + 12345u; return seed; }
static float rf(void) {
  u32 r = rnd();
  switch (r & 15) {
  case 0: return 0.0f;
  case 1: return -0.0f;
  case 2: return 1e-39f * (float)(r >> 20);      // denormal
  case 3: return 1.0f / 0.0f * ((r & 16) ? 1 : -1);  // inf (-> NaN paths)
  default: return ((float)(s32)(r >> 8) / 65536.0f) * ((r & 32) ? 1e-3f : 1.0f);
  }
}

static u32 checksum = 0;
static void mix(u32 v) { checksum = (checksum ^ v) * 16777619u; }

static void scalar_pass(u32 pass) {
  float a = rf(), b = rf(), c = rf(), d = rf();
  double x = (double)rf() * 1.000000119, y = (double)rf();
  for (u32 i = 0; i < 40; i++) {
    float s = a + b, t = a - c, u = b * c, v = a * b + c, w = a * b - d, q = (d != 0.0f) ? c / d : 0.5f;
    double m = x * y + 1.5, n2 = x / (y + 3.0), o = x - y * 0.25;
    s32 k = (s32)(u * 0.001f), kk = (s32)(m * 1e-3);
    mix(fbits(s)); mix(fbits(t)); mix(fbits(u)); mix(fbits(v)); mix(fbits(w)); mix(fbits(q));
    mix((u32)dbits(m)); mix((u32)(dbits(n2) >> 32)); mix((u32)dbits(o));
    mix((u32)k); mix((u32)kk); mix(a < b); mix(c >= d); mix(x > y);
    mix(fbits((float)x)); mix(fbits(-v)); mix(fbits(v < 0 ? -v : v));
    a = s * 0.5f + t; b = u - q * 0.25f; c = (float)(o * 0.001); d = v + (float)k;
    x = m * 0.125 + n2; y = o - (double)kk;
    if (!(a == a)) a = 1.0f; if (!(b == b)) b = 2.0f; if (!(c == c)) c = 3.0f; if (!(d == d)) d = 4.0f;
    if (!(x == x)) x = 5.0; if (!(y == y)) y = 6.0;
    if (a > 1e30f || a < -1e30f) a = 7.0f; if (b > 1e30f || b < -1e30f) b = 8.0f;
    if (x > 1e300 || x < -1e300) x = 9.0;
  }
  (void)pass;
}

void main_c(void) {
  VI16(0x00) = (240 << 4) | 6;
  VI32(0x0C) = (3u << 16) | 0x18;
  VI32(0x10) = (2u << 16) | 0x19;
  VI16(0x48) = (40 << 8) | 80;
  VI32(0x1C) = (1u << 28) | (XFB_PHYS >> 5);
  VI32(0x24) = (1u << 28) | ((XFB_PHYS + W * 2) >> 5);
  VI16(0x02) = 0x0001;
  fill(0, 0, W, H, BLUE);
  // paired singles on: HID2 = LSQE | WPE | PSE, GQR0 = float
  __asm__ volatile("lis 3, 0xE000\n mtspr 920, 3\n li 3, 0\n mtspr 912, 3" ::: "r3");
  for (u32 pass = 0; pass < 64; pass++) {
    if (pass == 32) __asm__ volatile("mtfsb1 29");  // FPSCR.NI for the second half
    scalar_pass(pass);
    for (u32 i = 0; i < 8 * 64; i++) fin[i] = rf();
    ps_kernel(fin, fout, 64);
    for (u32 i = 0; i < 46 * 64; i++) mix(fbits(fout[i]));
  }
  __asm__ volatile("mtfsb0 29");
  bits(300, checksum & 0xffff);
  bits(330, checksum >> 16);
  for (u32 it = 0;; it++) {
    scalar_pass(it);
    for (u32 i = 0; i < 8 * 64; i++) fin[i] = rf();
    ps_kernel(fin, fout, 64);
    bits(380, it);
  }
}
