  .text
  .globl scan_active
# u32 scan_active(u8* out, const u8* table): TTYD 0x8014505c loop shape
scan_active:
  li 7, 0           # count (r27 in the game)
  li 0, 64
  mr 4, 4
  li 5, 0
  mtctr 0
1:
  lhz 0, 0x90(4)
  rlwinm. 0, 0, 0, 16, 16
  beq- 2f
  stb 5, 0(3)
  addi 7, 7, 1
  addi 3, 3, 1
2:
  lhz 0, 0x128(4)
  addi 5, 5, 1
  rlwinm. 0, 0, 0, 16, 16
  beq- 3f
  stb 5, 0(3)
  addi 7, 7, 1
  addi 3, 3, 1
3:
  lhz 0, 0x1c0(4)
  addi 5, 5, 1
  rlwinm. 0, 0, 0, 16, 16
  beq- 4f
  stb 5, 0(3)
  addi 7, 7, 1
  addi 3, 3, 1
4:
  lhz 0, 0x258(4)
  addi 5, 5, 1
  rlwinm. 0, 0, 0, 16, 16
  beq- 5f
  stb 5, 0(3)
  addi 7, 7, 1
  addi 3, 3, 1
5:
  addi 5, 5, 1
  addi 4, 4, 608
  bdnz+ 1b
  mr 3, 7
  blr

  .globl nested
# u32 nested(u32 a, u32 b): nested forward branches, registers first used inside
nested:
  li 6, 1
  cmpwi 3, 0
  beq 3f            # outer skip
  addi 6, 6, 10
  cmpwi 4, 0
  beq 2f            # inner skip (target before outer target)
  mullw 8, 3, 4     # r8 first loaded/written inside the inner region
  add 6, 6, 8
  addi 9, 9, 1      # r9 read+written only inside
2:
  addi 6, 6, 100
3:
  add 3, 6, 9
  blr
