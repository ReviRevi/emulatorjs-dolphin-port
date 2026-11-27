  .section .text.start
  .globl _start
_start:
  lis 1, 0x8170        # stack at top of 24MB MEM1
  addi 1, 1, -16
  bl main_c
1: b 1b
