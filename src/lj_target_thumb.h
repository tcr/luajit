/*
** Definitions for ARM CPUs.
** Copyright (C) 2005-2014 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_TARGET_ARM_H
#define _LJ_TARGET_ARM_H

/* -- Registers IDs ------------------------------------------------------- */

#define GPRDEF(_) \
  _(R0) _(R1) _(R2) _(R3) _(R4) _(R5) _(R6) _(R7) \
  _(R8) _(R9) _(R10) _(R11) _(R12) _(SP) _(LR) _(PC)
#if LJ_SOFTFP
#define FPRDEF(_)
#else
#define FPRDEF(_) \
  _(D0) _(D1) _(D2) _(D3) _(D4) _(D5) _(D6) _(D7) \
  _(D8) _(D9) _(D10) _(D11) _(D12) _(D13) _(D14) _(D15)
#endif
#define VRIDDEF(_)

#define RIDENUM(name)	RID_##name,

enum {
  GPRDEF(RIDENUM)		/* General-purpose registers (GPRs). */
  FPRDEF(RIDENUM)		/* Floating-point registers (FPRs). */
  RID_MAX,
  RID_TMP = RID_LR,

  /* Calling conventions. */
  RID_RET = RID_R0,
  RID_RETLO = RID_R0,
  RID_RETHI = RID_R1,
#if LJ_SOFTFP
  RID_FPRET = RID_R0,
#else
  RID_FPRET = RID_D0,
#endif

  /* These definitions must match with the *.dasc file(s): */
  RID_BASE = RID_R9,		/* Interpreter BASE. */
  RID_LPC = RID_R6,		/* Interpreter PC. */
  RID_DISPATCH = RID_R7,	/* Interpreter DISPATCH table. */
  RID_LREG = RID_R8,		/* Interpreter L. */

  /* Register ranges [min, max) and number of registers. */
  RID_MIN_GPR = RID_R0,
  RID_MAX_GPR = RID_PC+1,
  RID_MIN_FPR = RID_MAX_GPR,
#if LJ_SOFTFP
  RID_MAX_FPR = RID_MIN_FPR,
#else
  RID_MAX_FPR = RID_D15+1,
#endif
  RID_NUM_GPR = RID_MAX_GPR - RID_MIN_GPR,
  RID_NUM_FPR = RID_MAX_FPR - RID_MIN_FPR
};

#define RID_NUM_KREF		RID_NUM_GPR
#define RID_MIN_KREF		RID_R0

/* -- Register sets ------------------------------------------------------- */

/* Make use of all registers, except sp, lr and pc. */
#define RSET_GPR		(RSET_RANGE(RID_MIN_GPR, RID_R12+1))
#define RSET_GPREVEN \
  (RID2RSET(RID_R0)|RID2RSET(RID_R2)|RID2RSET(RID_R4)|RID2RSET(RID_R6)| \
   RID2RSET(RID_R8)|RID2RSET(RID_R10))
#define RSET_GPRODD \
  (RID2RSET(RID_R1)|RID2RSET(RID_R3)|RID2RSET(RID_R5)|RID2RSET(RID_R7)| \
   RID2RSET(RID_R9)|RID2RSET(RID_R11))
#if LJ_SOFTFP
#define RSET_FPR		0
#else
#define RSET_FPR		(RSET_RANGE(RID_MIN_FPR, RID_MAX_FPR))
#endif
#define RSET_ALL		(RSET_GPR|RSET_FPR)
#define RSET_INIT		RSET_ALL

/* ABI-specific register sets. lr is an implicit scratch register. */
#define RSET_SCRATCH_GPR_	(RSET_RANGE(RID_R0, RID_R3+1)|RID2RSET(RID_R12))
#ifdef __APPLE__
#define RSET_SCRATCH_GPR	(RSET_SCRATCH_GPR_|RID2RSET(RID_R9))
#else
#define RSET_SCRATCH_GPR	RSET_SCRATCH_GPR_
#endif
#if LJ_SOFTFP
#define RSET_SCRATCH_FPR	0
#else
#define RSET_SCRATCH_FPR	(RSET_RANGE(RID_D0, RID_D7+1))
#endif
#define RSET_SCRATCH		(RSET_SCRATCH_GPR|RSET_SCRATCH_FPR)
#define REGARG_FIRSTGPR		RID_R0
#define REGARG_LASTGPR		RID_R3
#define REGARG_NUMGPR		4
#if LJ_ABI_SOFTFP
#define REGARG_FIRSTFPR		0
#define REGARG_LASTFPR		0
#define REGARG_NUMFPR		0
#else
#define REGARG_FIRSTFPR		RID_D0
#define REGARG_LASTFPR		RID_D7
#define REGARG_NUMFPR		8
#endif

/* -- Spill slots --------------------------------------------------------- */

/* Spill slots are 32 bit wide. An even/odd pair is used for FPRs.
**
** SPS_FIXED: Available fixed spill slots in interpreter frame.
** This definition must match with the *.dasc file(s).
**
** SPS_FIRST: First spill slot for general use. Reserve min. two 32 bit slots.
*/
#define SPS_FIXED	2
#define SPS_FIRST	2

#define SPOFS_TMP	0

#define sps_scale(slot)		(4 * (int32_t)(slot))
#define sps_align(slot)		(((slot) - SPS_FIXED + 1) & ~1)

/* -- Exit state ---------------------------------------------------------- */

/* This definition must match with the *.dasc file(s). */
typedef struct {
#if !LJ_SOFTFP
  lua_Number fpr[RID_NUM_FPR];	/* Floating-point registers. */
#endif
  int32_t gpr[RID_NUM_GPR];	/* General-purpose registers. */
  int32_t spill[256];		/* Spill slots. */
} ExitState;

/* PC after instruction that caused an exit. Used to find the trace number. */
#define EXITSTATE_PCREG		RID_PC
/* Highest exit + 1 indicates stack check. */
#define EXITSTATE_CHECKEXIT	1

#define EXITSTUB_SPACING        4
#define EXITSTUBS_PER_GROUP     32

/* -- Instructions -------------------------------------------------------- */

/* Instruction fields. */
#define ARMF_CC(ai, cc)	(((ai) ^ ARMI_CCAL) | ((cc) << 28))
#define ARMF_N(r)	((r) << 0)
#define ARMF_T(r) ((r) << 28)
#define ARMF_D(r)	((r) << 24)
#define ARMF_S(r)	((r) << 8)
#define ARMF_M(r)	((r) << 8)
#define ARMF_M2(r) ((r) << 16) // MOV
#define ARMF_M3(r) ((r) << 19) // BLXr
#define ARMF_SH(sh, n)	(((sh) << 20) | (((n) & 0x3) << 22) | ((((n) >> 2) & 0x7) << 28))
#define ARMF_RSH(sh, r)	(0x10 | ((sh) << 5) | ARMF_S(r))

/* Instruction compositing */
#define ARMY_SUB(arg, rsh, mask) (((arg)>>rsh)&(((1<<(mask))-1)))
#define ARMY_K12(A, B) ((A^ARMI_K12)|(((B)&0xff)<<16)|(((B)&0x700)<<20)|(((B)&0x800)>>1))
#define ARMY_K12_BARE(A, B) ((A)|(((B)&0xff)<<16)|(((B)&0x700)<<20)|(((B)&0x800)>>1))
#define ARMY_OP_BODY(A, B) ((A)^(B))
#define ARMY_B(A, B) ((A)|(ARMY_SUB(B,0,10)<<17)|ARMY_SUB(B,10,10)|(ARMY_SUB(B,20,1)<<27)|(ARMY_SUB(B,21,1)<<29)|(ARMY_SUB(B,22,1)<<10))
#define ARMY_B_READ(B) (((-ARMY_SUB(B,10,1)) & ~((1<<22)-1))|((((ARMY_SUB(B,16,11)<<0)|(ARMY_SUB(B,0,10)<<11)|(ARMY_SUB(B,27,1)<<21)|(ARMY_SUB(B,29,1)<<22)))>>1))
#define ARMY_OPREG(A) ((A)^0x1a00)
#define ARMY_OPK(A) ((A)^0x1a00)

// P U W and I flags
#define ARMY_FLAG(A, B) ((A)|(B))
#define ARMY_OFS(A, B) ((A)|((B)<<16))

#define ARMY_DNM(ai, rd, rn, rm) ((ai) | ARMF_D(rd) | ARMF_N(rn) | ARMF_M(rm))
#define ARMY_DNM2(ai, rd, rn, rm) ((ai) | ARMF_D(rd) | ARMF_N(rn) | ARMF_M2(rm))
#define ARMY_DN(ai, rd, rn) ((ai) | ARMF_D(rd) | ARMF_N(rn))
#define ARMY_TN(ai, rd, rn) ((ai) | ARMF_T(rd) | ARMF_N(rn))
#define ARMY_DM(ai, rd, rm) ((ai) | ARMF_D(rd) | ARMF_M(rm))
#define ARMY_DM2(ai, rd, rm) ((ai) | ARMF_D(rd) | ARMF_M2(rm))
#define ARMY_NM(ai, rn, rm) ((ai) | ARMF_N(rn) | ARMF_M(rm))
#define ARMY_NM2(ai, rn, rm) ((ai) | ARMF_N(rn) | ARMF_M2(rm))
#define ARMY_D(ai, rd) ((ai) | ARMF_D(rd))
#define ARMY_N(ai, rn) ((ai) | ARMF_N(rn))
#define ARMY_M(ai, rm) ((ai) | ARMF_M(rm))
#define ARMY_M3(ai, rm) ((ai) | ARMF_M3(rm))
#define ARMY_T(ai, rt) ((ai) | (ARMF_T(rt)))
// shifts, flags
#define ARMY_SH(A, B, S) ((A)|(ARMF_SH(B, S)))
#define ARMY_RSH(A, B, S) ((A)|(ARMF_RSH(B, S)))
#define ARMY_COND(A) ((A)|(ARMI_S))
// CC
#define ARMY_CC_IS(A) (((A))>>28)
#define ARMY_CC_REPLACE(A, B, C) A ^= ((B^C) << 28)
#define ARMY_CCB(A, B) (A)
// IT instr
#define ARMY_IT(cc) *--as->mcp = (0xbf08bf00u | (((cc)&0xf) << 20))
// compare
#define ARMY_IS(A, B) (((A) & ~ARMI_S) == (B))
// reverse operand order
#define ARMY_REVERSE(A) A ^= (ARMI_SUB)^(ARMI_RSB)
// see if BODY has changed its op
#define ARMY_MOD_OP(A) (A & ((ARMI_AND)^(ARMI_BIC)))
// P U W and I flags
#define ARMY_FLAG(A, B) ((A)|(B))
// ldr/str checks
#define ARMY_ISVFP(A) ((A)&0x08000000)
#define ARMY_HWORD(A) ((A)&0x04000000)
// MOVT and MOVW
#define ARMY_MOVTW(A, K) (A | (ARMY_SUB(K, 0, 8) << 16) | (ARMY_SUB(K, 8, 3) << 28) | (ARMY_SUB(K, 11, 1) << 10) | (ARMY_SUB(K, 12, 4)))

#define ARMY_NODEF 0xffffffff

typedef enum ARMIns {
  ARMI_CCAL = 0xe0000000,

  // ARMI_S = 0x000100000,
  ARMI_S = 1 << 4,

  // ARMI_K12 = 0x02000000,
  ARMI_K12 = 0x00001a00,

  ARMI_KNEG = 0x00200000,
  ARMI_LS_W = 0x00200000,

  ARMI_LS_U = 0x02000000,
  // ARMI_LS_U = 0x00800000,
  ARMI_LS_P = 0x04000000,
  // ARMI_LS_P = 0x01000000,
  ARMI_LS_R = 0x02000000,
  ARMI_LSX_I = 0x00000040,

  // 11110H00000snnnn0HHHddddHHHHHHHH
  // 11101010000snnnn0iiiddddiiTTmmmm
  ARMI_AND = 0x0000ea00,
  // ARMI_AND = 0xe0000000,
  
  // 11110H00100snnnn0HHHddddHHHHHHHH
  // 11101010100snnnn0iiiddddiiTTmmmm
  ARMI_EOR = 0x0000ea80,
  // ARMI_EOR = 0xe0200000,
  
  // 11110H01101snnnn0HHHddddHHHHHHHH
  // 11101011101snnnn0iiiddddiiTTmmmm
  ARMI_SUB = 0x0000eba0,
  // ARMI_SUB = 0xe0400000,

  // 11110H01110snnnn0HHHddddHHHHHHHH
  // 11101011110snnnn0iiiddddiiTTmmmm
  ARMI_RSB = 0x0000ebc0,
  // ARMI_RSB = 0xe0600000,
  
  // 11110H01000snnnn0HHHddddHHHHHHHH
  // 11101011000snnnn0iiiddddiiTTmmmm
  ARMI_ADD = 0x0000eb00,
  // ARMI_ADD = 0xe0800000,

  // 11110H01010snnnn0HHHddddHHHHHHHH
  // 11101011010snnnn0iiiddddiiTTmmmm
  ARMI_ADC = 0x0000eb40,
  // ARMI_ADC = 0xe0a00000,

  // 11110H01011snnnn0HHHddddHHHHHHHH
  // 11101011011snnnn0iiiddddiiTTmmmm
  ARMI_SBC = 0x0000eb60,
  // ARMI_SBC = 0xe0c00000,

  // TODO fix this on thumb
  // ARMI_RSC = ARMY_NODEF,
  // ARMI_RSC = 0xe0e00000,

  // h 11110H000001nnnn0HHH1111HHHHHHHH
  // r 111010100001nnnn0iii1111iiTTmmmm
  ARMI_TST = 0x0f00ea10,
  // ARMI_TST = 0xe1100000,

  // 11110H001001nnnn0HHH1111HHHHHHHH
  // 111010101001nnnn0iii1111iiTTmmmm
  ARMI_TEQ = 0x0f00ea90,
  // ARMI_TEQ = 0xe1300000,

  // 11110H011011nnnn0HHH1111HHHHHHHH
  // 111010111011nnnn0iii1111iiTTmmmm
  ARMI_CMP = 0x0f00ebb0,
  // ARMI_CMP = 0xe1500000,
  
  // 11110H010001nnnn0HHH1111HHHHHHHH
  // 111010110001nnnn0iii1111iiTTmmmm
  ARMI_CMN = 0x0f00eb10,
  // ARMI_CMN = 0xe1700000,

  // 11110H00010snnnn0HHHddddHHHHHHHH
  // 11101010010snnnn0iiiddddiiTTmmmm
  ARMI_ORR = 0x0000ea40,
  // ARMI_ORR = 0x0000ea40,
  
  // 11110H00010s11110HHHddddHHHHHHHH
  // 11101010010s11110000dddd0000mmmm
  ARMI_MOV = 0x0000ea4f,
  // ARMI_MOV = 0xe1a00000,

  // 11110H00001snnnn0HHHddddHHHHHHHH
  // 11101010001snnnn0iiiddddiiTTmmmm
  ARMI_BIC = 0x0000ea20,
  // ARMI_BIC = 0xe1c00000,

  // 11110H00011s11110HHHddddHHHHHHHH
  // 11101010011s11110iiiddddiiTTmmmm
  ARMI_MVN = 0x0000ea6f,
  // ARMI_MVN = 0xe1e00000,

  // 11110011101011111000000000000000
  ARMI_NOP = 0xbf00bf00,
  // ARMI_NOP = 0xe1a00000,

  // -
  // 111110110000nnnn1111dddd0000mmmm
  ARMI_MUL = 0xf000fb00,
  // ARMI_MUL = 0xe0000090,

  // -
  // 111110111000nnnnllllhhhh0000mmmm
  ARMI_SMULL = 0x0000fb80,
  // ARMI_SMULL = 0xe0c00090,

  // p tL:111110000101nnnntttt1PUWiiiiiiii
  // i tL:111110001101nnnnttttiiiiiiiiiiii
  // r tL:111110000101nnnntttt000000iimmmm
  // x tB:11111000u1011111ttttiiiiiiiiiiii
  ARMI_LDR = 0x0800f850,
  ARMI_LDRp = 0x0800f850,
  ARMI_LDRi = 0x0000f8d0,
  ARMI_LDRr = 0x0000f850,
  // ARMI_LDR = 0xe4100000,

  // i tL:111110001101nnnnttttiiiiiiiiiiii
  ARMI_LDRT = 0x0000f8d0,

  // p tL:111110000001nnnntttt1PUWiiiiiiii
  // i tL:111110001001nnnnttttiiiiiiiiiiii
  // r tL:111110000001nnnntttt000000iimmmm
  // x tB:11111000u0011111ttttiiiiiiiiiiii
  ARMI_LDRB = 0x0800f810,
  ARMI_LDRBp = 0x0800f810,
  ARMI_LDRBi = 0x0000f890,
  ARMI_LDRBr = 0x0000f810,
  // ARMI_LDRB = 0xe4500000,

  // p tL:111110000011nnnntttt1PUWiiiiiiii
  // i tL:111110001011nnnnttttiiiiiiiiiiii
  // r tL:111110000011nnnntttt000000iimmmm
  // x tB:11111000u0111111ttttiiiiiiiiiiii
  ARMI_LDRH = 0x0800f830,
  ARMI_LDRHp = 0x0800f830,
  ARMI_LDRHi = 0x0000f8b0,
  ARMI_LDRHr = 0x0000f830,
  // ARMI_LDRH = 0xe01000b0,

  // p tL:111110010001nnnntttt1PUWiiiiiiii
  // i tL:111110011001nnnnttttiiiiiiiiiiii
  // r tL:111110010001nnnntttt000000iimmmm
  ARMI_LDRSB = 0x0800f910,
  ARMI_LDRSBp = 0x0800f910,
  ARMI_LDRSBi = 0x0000f990,
  ARMI_LDRSBr = 0x0000f910,
  // ARMI_LDRSB = 0xe01000d0,

  // p tL:111110010011nnnntttt1PUWiiiiiiii
  // i tL:111110011011nnnnttttiiiiiiiiiiii
  // r tL:111110010011nnnntttt000000iimmmm
  // x tB:11111001u0111111ttttiiiiiiiiiiii
  ARMI_LDRSH = 0x0800f930,
  ARMI_LDRSHp = 0x0800f930,
  ARMI_LDRSHi = 0x0000f9b0,
  ARMI_LDRSHr = 0x0000f930,
  // ARMI_LDRSH = 0xe01000f0,

  // i tdB:1110100PU1W11111ttttddddiiiiiiii
  // f tdLi:1110100PU1W1nnnnttttddddffffffff
  ARMI_LDRD = 0x0000e85f,
  ARMI_LDRDi = 0x0000e85f,
  ARMI_LDRDf = 0x0000e850,
  // ARMI_LDRD = 0xe00000d0,
  
  // p tL:111110000100nnnntttt1PUWiiiiiiii
  // i tL:111110001100nnnnttttiiiiiiiiiiii
  // r tL:111110000100nnnntttt000000iimmmm
  ARMI_STR = 0x0800f840,
  ARMI_STRp = 0x0800f840,
  ARMI_STRi = 0x0000f8c0,
  ARMI_STRr = 0x0000f840,
  // ARMI_STR = 0xe4000000,

  // p tL:111110000000nnnntttt1PUWiiiiiiii
  // i tL:111110001000nnnnttttiiiiiiiiiiii
  // r tL:111110000000nnnntttt000000iimmmm
  ARMI_STRB = 0x0800f800,
  ARMI_STRBp = 0x0800f800,
  ARMI_STRBi = 0x0000f880,
  ARMI_STRBr = 0x0000f800,
  // ARMI_STRB = 0xe4400000,

  // p tL:111110000010nnnntttt1PUWiiiiiiii
  // i tL:111110001010nnnnttttiiiiiiiiiiii
  ARMI_STRH = 0x0800f820,
  ARMI_STRHp = 0x0800f820,
  ARMI_STRHi = 0x0000f8a0,
  // ARMI_STRH = 0xe00000b0,

  // tdL:1110100PU1W0nnnnttttddddffffffff
  ARMI_STRD = 0x0000e840,
  // ARMI_STRD = 0xe00000f0,

  // r r:1110100100101101rrrrrrrrrrrrrrrr
  // t t:1111100001001101tttt110100000100
  ARMI_PUSH = 0x0000e92d,
  // ARMI_PUSH = 0xe92d0000,

  // 11110scccciiiiii10j0kiiiiiiiiiii
  // 11110siiiiiiiiii10j1kiiiiiiiiiii
  ARMI_B = 0xb800f000,
  // ARMI_B = 0xea000000,

  // 11110siiiiiiiiii11J1Kiiiiiiiiiii
  ARMI_BL = 0xf800f000,
  // ARMI_BL = 0xeb000000,

  // 010001111mmmm000
  ARMI_BLX = 0x4780bf00,
  // ARMI_BLX = 0xfa000000,

  // 010001111mmmm000
  ARMI_BLXr = 0x4780bf00,
  // ARMI_BLXr = 0xe12fff30,

  /* ARMv6 */
  // 111110101001mmmm1111dddd1000xxxx
  ARMI_REV = 0xf080fa90,
  // ARMI_REV = 0xe6bf0f30,
  // 11111010010011111111dddd10rrmmmm
  ARMI_SXTB = 0xf080fa4f,
  // ARMI_SXTB = 0xe6af0070,
  // 11111010000011111111dddd10rrmmmm
  ARMI_SXTH = 0xf080fa0f,
  // ARMI_SXTH = 0xe6bf0070,
  // 11111010010111111111dddd10rrmmmm
  ARMI_UXTB = 0xf080fa5f,
  // ARMI_UXTB = 0xe6ef0070,
  // 11111010000111111111dddd10rrmmmm
  ARMI_UXTH = 0xf080fa1f,
  // ARMI_UXTH = 0xe6ff0070,

  /* ARMv6T2 */
  // 11110H100100kkkk0HHHddddHHHHHHHH
  ARMI_MOVW = 0x0000f240,
  // ARMI_MOVW = 0xe3000000,
  // 11110H101100kkkk0HHHddddHHHHHHHH
  ARMI_MOVT = 0x0000f2c0,
  // ARMI_MOVT = 0xe3400000,

  /* VFP */
  ARMI_VMOV_D = ARMY_NODEF,
  ARMI_VMOV_S = ARMY_NODEF,
  ARMI_VMOVI_D = ARMY_NODEF,

  ARMI_VMOV_R_S = ARMY_NODEF,
  ARMI_VMOV_S_R = ARMY_NODEF,
  ARMI_VMOV_RR_D = ARMY_NODEF,
  ARMI_VMOV_D_RR = ARMY_NODEF,

  ARMI_VADD_D = ARMY_NODEF,
  ARMI_VSUB_D = ARMY_NODEF,
  ARMI_VMUL_D = ARMY_NODEF,
  ARMI_VMLA_D = ARMY_NODEF,
  ARMI_VMLS_D = ARMY_NODEF,
  ARMI_VNMLS_D = ARMY_NODEF,
  ARMI_VDIV_D = ARMY_NODEF,

  ARMI_VABS_D = ARMY_NODEF,
  ARMI_VNEG_D = ARMY_NODEF,
  ARMI_VSQRT_D = ARMY_NODEF,

  ARMI_VCMP_D = ARMY_NODEF,
  ARMI_VCMPZ_D = ARMY_NODEF,

  ARMI_VMRS = ARMY_NODEF,

  ARMI_VCVT_S32_F32 = ARMY_NODEF,
  ARMI_VCVT_S32_F64 = ARMY_NODEF,
  ARMI_VCVT_U32_F32 = ARMY_NODEF,
  ARMI_VCVT_U32_F64 = ARMY_NODEF,
  ARMI_VCVTR_S32_F32 = ARMY_NODEF,
  ARMI_VCVTR_S32_F64 = ARMY_NODEF,
  ARMI_VCVTR_U32_F32 = ARMY_NODEF,
  ARMI_VCVTR_U32_F64 = ARMY_NODEF,
  ARMI_VCVT_F32_S32 = ARMY_NODEF,
  ARMI_VCVT_F64_S32 = ARMY_NODEF,
  ARMI_VCVT_F32_U32 = ARMY_NODEF,
  ARMI_VCVT_F64_U32 = ARMY_NODEF,
  ARMI_VCVT_F32_F64 = ARMY_NODEF,
  ARMI_VCVT_F64_F32 = ARMY_NODEF,

  ARMI_VLDR_S = ARMY_NODEF,
  ARMI_VLDR_D = ARMY_NODEF,
  ARMI_VSTR_S = ARMY_NODEF,
  ARMI_VSTR_D = ARMY_NODEF,
} ARMIns;

typedef enum ARMShift {
  ARMSH_LSL, ARMSH_LSR, ARMSH_ASR, ARMSH_ROR
} ARMShift;

/* ARM condition codes. */
typedef enum ARMCC {
  CC_EQ, CC_NE, CC_CS, CC_CC, CC_MI, CC_PL, CC_VS, CC_VC,
  CC_HI, CC_LS, CC_GE, CC_LT, CC_GT, CC_LE, CC_AL,
  CC_HS = CC_CS, CC_LO = CC_CC
} ARMCC;

#endif
