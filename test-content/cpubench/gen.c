#include <stdio.h>
#include "work.h"
int main(void) { printf("static const u32 expected[32] = {"); for (u32 i = 0; i < 32; i++) printf("0x%08xu,", work(i)); printf("};\n"); return 0; }
