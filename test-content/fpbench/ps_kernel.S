  .text
  .globl ps_kernel
ps_kernel:
  mtctr 5
1:
  .long 0xe0230000  # psq_l f1,0(r3)
  .long 0xe0430008  # psq_l f2,8(r3)
  .long 0xe0630010  # psq_l f3,16(r3)
  .long 0xe0838018  # psq_l f4,24(r3),W=1
  .long 0x10a1102a  # ps_add f5,f1,f2
  .long 0x10c11828  # ps_sub f6,f1,f3
  .long 0x10e100b2  # ps_mul f7,f1,f2
  .long 0x110118ba  # ps_madd f8,f1,f2,f3
  .long 0x112208f8  # ps_msub f9,f2,f3,f1
  .long 0x1143107e  # ps_nmadd f10,f3,f1,f2
  .long 0x1161113c  # ps_nmsub f11,f1,f4,f2
  .long 0x11811024  # ps_div f12,f1,f2
  .long 0x11a531d4  # ps_sum0 f13,f5,f7,f6
  .long 0x10084a96  # ps_sum1 f0,f8,f10,f9
  .long 0xf0a40000  # psq_st f5,0(r4)
  .long 0xf0c40008  # psq_st f6,8(r4)
  .long 0xf0e40010  # psq_st f7,16(r4)
  .long 0xf1040018  # psq_st f8,24(r4)
  .long 0xf1240020  # psq_st f9,32(r4)
  .long 0xf1440028  # psq_st f10,40(r4)
  .long 0xf1640030  # psq_st f11,48(r4)
  .long 0xf1840038  # psq_st f12,56(r4)
  .long 0xf1a40040  # psq_st f13,64(r4)
  .long 0xf0040048  # psq_st f0,72(r4)
  .long 0x10a10098  # ps_muls0 f5,f1,f2
  .long 0x10c200da  # ps_muls1 f6,f2,f3
  .long 0x10e1209c  # ps_madds0 f7,f1,f2,f4
  .long 0x1103089e  # ps_madds1 f8,f3,f2,f1
  .long 0x112610ee  # ps_sel f9,f6,f3,f2
  .long 0x11400850  # ps_neg f10,f1
  .long 0x11603210  # ps_abs f11,f6
  .long 0x11800910  # ps_nabs f12,f1
  .long 0x11a11420  # ps_merge00 f13,f1,f2
  .long 0x10021c60  # ps_merge01 f0,f2,f3
  .long 0xf0a40050  # psq_st f5,80(r4)
  .long 0xf0c40058  # psq_st f6,88(r4)
  .long 0xf0e40060  # psq_st f7,96(r4)
  .long 0xf1040068  # psq_st f8,104(r4)
  .long 0xf1240070  # psq_st f9,112(r4)
  .long 0xf1440078  # psq_st f10,120(r4)
  .long 0xf1640080  # psq_st f11,128(r4)
  .long 0xf1840088  # psq_st f12,136(r4)
  .long 0xf1a40090  # psq_st f13,144(r4)
  .long 0xf0040098  # psq_st f0,152(r4)
  .long 0x10a114a0  # ps_merge10 f5,f1,f2
  .long 0x10c21ce0  # ps_merge11 f6,f2,f3
  .long 0xf0a400a0  # psq_st f5,160(r4)
  .long 0xf0c400a8  # psq_st f6,168(r4)
  .long 0xf08480b0  # psq_st f4,176(r4),W=1
  .long 0x10811000  # ps_cmpu0 cr1,f1,f2
  .long 0x11021880  # ps_cmpu1 cr2,f2,f3
  .long 0x11830840  # ps_cmpo0 cr3,f3,f1
  mfcr 6
  stw 6, 180(4)
  addi 3, 3, 32
  addi 4, 4, 184
  bdnz 1b
  blr
