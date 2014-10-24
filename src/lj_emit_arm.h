/*
** ARM instruction emitter.
** Copyright (C) 2005-2014 Mike Pall. See Copyright Notice in luajit.h
*/

#define _W(A) A

// P U W and I flags
#define ARMY_FLAG(A, B) (_W(A)|_W(B))
#define ARMY_OFS(A, B) (_W(A)|(_W(B)<<16))

#define ARMY_DNM(ai, rd, rn, rm) ((ai) | ARMF_D(rd) | ARMF_N(rn) | ARMF_M(rm))
#define ARMY_DN(ai, rd, rn) ((ai) | ARMF_D(rd) | ARMF_N(rn))
#define ARMY_TN(ai, rd, rn) ((ai) | ARMF_T(rd) | ARMF_N(rn))
#define ARMY_DM(ai, rd, rm) ((ai) | ARMF_D(rd) | ARMF_M(rm))
#define ARMY_DM2(ai, rd, rm) ((ai) | ARMF_D(rd) | ARMF_M2(rm))
#define ARMY_NM(ai, rn, rm) ((ai) | ARMF_N(rn) | ARMF_M(rm))
#define ARMY_D(ai, rd) ((ai) | ARMF_D(rd))
#define ARMY_N(ai, rn) ((ai) | ARMF_N(rn))
#define ARMY_M(ai, rm) ((ai) | ARMF_M(rm))

/* -- Constant encoding --------------------------------------------------- */

static uint8_t emit_invai[16] = {
  /* AND */ (ARMI_AND^ARMI_BIC) >> 21,
  /* EOR */ 0,
  /* SUB */ (ARMI_SUB^ARMI_ADD) >> 21,
  /* RSB */ 0,
  /* ADD */ (ARMI_ADD^ARMI_SUB) >> 21,
  /* ADC */ (ARMI_ADC^ARMI_SBC) >> 21,
  /* SBC */ (ARMI_SBC^ARMI_ADC) >> 21,
  /* RSC */ 0,
  /* TST */ 0,
  /* TEQ */ 0,
  /* CMP */ (ARMI_CMP^ARMI_CMN) >> 21,
  /* CMN */ (ARMI_CMN^ARMI_CMP) >> 21,
  /* ORR */ 0,
  /* MOV */ (ARMI_MOV^ARMI_MVN) >> 21,
  /* BIC */ (ARMI_BIC^ARMI_AND) >> 21,
  /* MVN */ (ARMI_MVN^ARMI_MOV) >> 21
};

/* Encode constant in K12 format for data processing instructions. */
static uint32_t emit_isk12(ARMIns ai, int32_t n)
{
  uint32_t invai, i, m = (uint32_t)n;
  /* K12: unsigned 8 bit value, rotated in steps of one bit. */
  for (i = 0; i < 4096; i += 128, m = lj_rol(m, 1))
    if (m <= 255) {
      if (m & 0x80 && i > 128*8) return ARMY_K12(0, i|(m & 0x7f));
      else if (i < 128*8) return ARMY_K12(0, m & 0xff);
    }
  /* Otherwise try negation/complement with the inverse instruction. */
  invai = emit_invai[((ai >> 21) & 15)];
  if (!invai) return 0;  /* Failed. No inverse instruction. */
  m = ~(uint32_t)n;
  if (invai == ((ARMI_SUB^ARMI_ADD) >> 21) ||
      invai == (ARMI_CMP^ARMI_CMN) >> 21) m++;
  for (i = 0; i < 4096; i += 128, m = lj_rol(m, 1))
    if (m <= 255) {
      if (m & 0x80 && i > 128*8) return ARMY_K12(invai<<21, i|(m & 0x7f));
      else if (i < 128*8) return ARMY_K12(invai<<21, m & 0xff);
    }
  return 0;  /* Failed. */
}

/* -- Emit basic instructions --------------------------------------------- */

static void emit_dnm(ASMState *as, ARMIns ai, Reg rd, Reg rn, Reg rm)
{
  *--as->mcp = ARMY_DNM(ai, rd, rn, rm);
}

static void emit_dm2(ASMState *as, ARMIns ai, Reg rd, Reg rm)
{
  *--as->mcp = ARMY_DM2(ai, rd, rm);
}

static void emit_dm(ASMState *as, ARMIns ai, Reg rd, Reg rm)
{
  *--as->mcp = ARMY_DM(ai, rd, rm);
}

static void emit_dn(ASMState *as, ARMIns ai, Reg rd, Reg rn)
{
  *--as->mcp = ARMY_DN(ai, rd, rn);
}

static void emit_nm(ASMState *as, ARMIns ai, Reg rn, Reg rm)
{
  *--as->mcp = ARMY_NM(ai, rn, rm);
}

static void emit_d(ASMState *as, ARMIns ai, Reg rd)
{
  *--as->mcp = ARMY_D(ai, rd);
}

static void emit_n(ASMState *as, ARMIns ai, Reg rn)
{
  *--as->mcp = ARMY_N(ai, rn);
}

static void emit_m(ASMState *as, ARMIns ai, Reg rm)
{
  *--as->mcp = ARMY_M(ai, rm);
}

static void emit_lsox(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lua_assert(ofs >= -255 && ofs <= 255);
  if (ofs < 0) ofs = -ofs; else ai |= ARMI_LS_U;
  *--as->mcp = ARMY_OFS(ARMY_DN(ARMY_FLAG(ai, ARMI_LS_P | ARMI_LSX_I), rd, rn),
	       ((ofs & 0xf0) << 4) | (ofs & 0x0f));
}

static void emit_lso(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lua_assert(ofs >= -4095 && ofs <= 4095);
  /* Combine LDR/STR pairs to LDRD/STRD. */
  if (*as->mcp == ARMY_OFS(ARMY_DN(ARMY_FLAG(ai, ARMI_LS_P|ARMI_LS_U), rd^1, rn),ofs^4) &&
      (ai & ~(ARMI_LDR^ARMI_STR)) == ARMI_STR && rd != rn &&
      (uint32_t)ofs <= 252 && !(ofs & 3) && !((rd ^ (ofs >>2)) & 1) &&
      as->mcp != as->mcloop) {
    as->mcp++;
    emit_lsox(as, ai == ARMI_LDR ? ARMI_LDRD : ARMI_STRD, rd&~1, rn, ofs&~4);
    return;
  }
  if (ofs < 0) ofs = -ofs; else ai = ARMY_FLAG(ai, ARMI_LS_U);
  *--as->mcp = ARMY_OFS(ARMY_TN(ARMY_FLAG(ai, ARMI_LS_P), rd, rn), ofs);
}

#if !LJ_SOFTFP
static void emit_vlso(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lua_assert(ofs >= -1020 && ofs <= 1020 && (ofs&3) == 0);
  if (ofs < 0) ofs = -ofs; else ai |= ARMI_LS_U;
  *--as->mcp = ARMY_OFS(ARMY_DN(ARMY_FLAG(ai, ARMI_LS_P), rd & 15, rn), ofs >> 2);
}
#endif

/* -- Emit loads/stores --------------------------------------------------- */

/* Prefer spills of BASE/L. */
#define emit_canremat(ref)	((ref) < ASMREF_L)

/* Try to find a one step delta relative to another constant. */
static int emit_kdelta1(ASMState *as, Reg d, int32_t i)
{
  RegSet work = ~as->freeset & RSET_GPR;
  while (work) {
    Reg r = rset_picktop(work);
    IRRef ref = regcost_ref(as->cost[r]);
    lua_assert(r != d);
    if (emit_canremat(ref)) {
      int32_t delta = i - (ra_iskref(ref) ? ra_krefk(as, ref) : IR(ref)->i);
      uint32_t k = emit_isk12(ARMI_ADD, delta);
      if (k) {
	if (k == ARMI_K12)
	  emit_dm(as, ARMI_MOV, d, r);
	else
	  emit_dn(as, ARMY_OP_BODY(ARMI_ADD, k), d, r);
	return 1;
      }
    }
    rset_clear(work, r);
  }
  return 0;  /* Failed. */
}

/* Try to find a two step delta relative to another constant. */
static int emit_kdelta2(ASMState *as, Reg d, int32_t i)
{
  RegSet work = ~as->freeset & RSET_GPR;
  while (work) {
    Reg r = rset_picktop(work);
    IRRef ref = regcost_ref(as->cost[r]);
    lua_assert(r != d);
    if (emit_canremat(ref)) {
      int32_t other = ra_iskref(ref) ? ra_krefk(as, ref) : IR(ref)->i;
      if (other) {
	int32_t delta = i - other;
	uint32_t sh, inv = 0, k2, k;
	if (delta < 0) { delta = -delta; inv = ARMI_ADD^ARMI_SUB; }
	sh = lj_ffs(delta) & ~1;
	k2 = emit_isk12(0, delta & (255 << sh));
	k = emit_isk12(0, delta & ~(255 << sh));
	if (k) {
	  emit_dn(as, ARMY_OP_BODY(ARMI_ADD^inv, k2), d, d);
	  emit_dn(as, ARMY_OP_BODY(ARMI_ADD^inv, k), d, r);
	  return 1;
	}
      }
    }
    rset_clear(work, r);
  }
  return 0;  /* Failed. */
}

/* Load a 32 bit constant into a GPR. */
static void emit_loadi(ASMState *as, Reg r, int32_t i)
{
  uint32_t k = emit_isk12(ARMI_MOV, i);
  lua_assert(rset_test(as->freeset, r) || r == RID_TMP);
  if (k) {
    /* Standard K12 constant. */
    emit_d(as, ARMI_MOV^k, r);
  } else if ((as->flags & JIT_F_ARMV6T2) && (uint32_t)i < 0x00010000u) {
    /* 16 bit loword constant for ARMv6T2. */
    emit_d(as, ARMI_MOVW|(i & 0x0fff)|((i & 0xf000)<<4), r);
  } else if (emit_kdelta1(as, r, i)) {
    /* One step delta relative to another constant. */
  } else if ((as->flags & JIT_F_ARMV6T2)) {
    /* 32 bit hiword/loword constant for ARMv6T2. */
    emit_d(as, ARMI_MOVT|((i>>16) & 0x0fff)|(((i>>16) & 0xf000)<<4), r);
    emit_d(as, ARMI_MOVW|(i & 0x0fff)|((i & 0xf000)<<4), r);
  } else if (emit_kdelta2(as, r, i)) {
    /* Two step delta relative to another constant. */
  } else {
    /* Otherwise construct the constant with up to 4 instructions. */
    /* NYI: use mvn+bic, use pc-relative loads. */
    for (;;) {
      uint32_t sh = lj_ffs(i) & ~1;
      int32_t m = i & (255 << sh);
      i &= ~(255 << sh);
      if (i == 0) {
	emit_d(as, ARMY_OP_BODY(ARMI_MOV, emit_isk12(0, m)), r);
	break;
      }
      emit_dn(as, ARMY_OP_BODY(ARMI_ORR, emit_isk12(0, m)), r, r);
    }
  }
}

#define emit_loada(as, r, addr)		emit_loadi(as, (r), i32ptr((addr)))

static Reg ra_allock(ASMState *as, int32_t k, RegSet allow);

/* Get/set from constant pointer. */
static void emit_lsptr(ASMState *as, ARMIns ai, Reg r, void *p)
{
  int32_t i = i32ptr(p);
  emit_lso(as, ai, r, ra_allock(as, (i & ~4095), rset_exclude(RSET_GPR, r)),
	   (i & 4095));
}

#if !LJ_SOFTFP
/* Load a number constant into an FPR. */
static void emit_loadn(ASMState *as, Reg r, cTValue *tv)
{
  int32_t i;
  if ((as->flags & JIT_F_VFPV3) && !tv->u32.lo) {
    uint32_t hi = tv->u32.hi;
    uint32_t b = ((hi >> 22) & 0x1ff);
    if (!(hi & 0xffff) && (b == 0x100 || b == 0x0ff)) {
      *--as->mcp = ARMY_D(ARMI_VMOVI_D, r & 15) |
		   ((tv->u32.hi >> 12) & 0x00080000) |
		   ((tv->u32.hi >> 4) & 0x00070000) |
		   ((tv->u32.hi >> 16) & 0x0000000f);
      return;
    }
  }
  i = i32ptr(tv);
  emit_vlso(as, ARMI_VLDR_D, r,
	    ra_allock(as, (i & ~1020), RSET_GPR), (i & 1020));
}
#endif

/* Get/set global_State fields. */
#define emit_getgl(as, r, field) \
  emit_lsptr(as, ARMI_LDR, (r), (void *)&J2G(as->J)->field)
#define emit_setgl(as, r, field) \
  emit_lsptr(as, ARMI_STR, (r), (void *)&J2G(as->J)->field)

/* Trace number is determined from pc of exit instruction. */
#define emit_setvmstate(as, i)		UNUSED(i)

/* -- Emit control-flow instructions -------------------------------------- */

/* Label for internal jumps. */
typedef MCode *MCLabel;

/* Return label pointing to current PC. */
#define emit_label(as)		((as)->mcp)

static void emit_branch(ASMState *as, ARMIns ai, MCode *target)
{
  MCode *p = as->mcp;
  ptrdiff_t delta = (target - p) - 1;
  lua_assert(((delta + 0x00800000) >> 24) == 0);
  *--p = ARMY_B(ai, (uint32_t)delta);
  as->mcp = p;
}

#define emit_jmp(as, target) emit_branch(as, ARMI_B, (target))

static void emit_call(ASMState *as, void *target)
{
  MCode *p = --as->mcp;
  ptrdiff_t delta = ((char *)target - (char *)p) - 8;
  if ((((delta>>2) + 0x00800000) >> 24) == 0) {
    if ((delta & 1))
      *p = ARMY_B(ARMI_BLX, (uint32_t)(delta>>2)) | ((delta&2) << 27);
    else
      *p = ARMY_B(ARMI_BL, (uint32_t)(delta>>2));
  } else {  /* Target out of range: need indirect call. But don't use R0-R3. */
    Reg r = ra_allock(as, i32ptr(target), RSET_RANGE(RID_R4, RID_R12+1));
    *p = ARMY_M(ARMI_BLXr, r);
  }
}

/* -- Emit generic operations --------------------------------------------- */

/* Generic move between two regs. */
static void emit_movrr(ASMState *as, IRIns *ir, Reg dst, Reg src)
{
#if LJ_SOFTFP
  lua_assert(!irt_isnum(ir->t)); UNUSED(ir);
#else
  if (dst >= RID_MAX_GPR) {
    emit_dm(as, irt_isnum(ir->t) ? ARMI_VMOV_D : ARMI_VMOV_S,
	    (dst & 15), (src & 15));
    return;
  }
#endif
  if (as->mcp != as->mcloop) {  /* Swap early registers for loads/stores. */
    MCode ins = *as->mcp, swp = (src^dst);
    if ((ins & 0x0c000000) == 0x04000000 && (ins & 0x02000010) != 0x02000010) {
      if (!((ins ^ (dst << 16)) & 0x000f0000))
	*as->mcp = ins ^ (swp << 16);  /* Swap N in load/store. */
      if (!(ins & 0x00100000) && !((ins ^ (dst << 12)) & 0x0000f000))
	*as->mcp = ins ^ (swp << 12);  /* Swap D in store. */
    }
  }
  emit_dm2(as, ARMI_MOV, dst, src);
}

/* Generic load of register from stack slot. */
static void emit_spload(ASMState *as, IRIns *ir, Reg r, int32_t ofs)
{
#if LJ_SOFTFP
  lua_assert(!irt_isnum(ir->t)); UNUSED(ir);
#else
  if (r >= RID_MAX_GPR)
    emit_vlso(as, irt_isnum(ir->t) ? ARMI_VLDR_D : ARMI_VLDR_S, r, RID_SP, ofs);
  else
#endif
    emit_lso(as, ARMI_LDR, r, RID_SP, ofs);
}

/* Generic store of register to stack slot. */
static void emit_spstore(ASMState *as, IRIns *ir, Reg r, int32_t ofs)
{
#if LJ_SOFTFP
  lua_assert(!irt_isnum(ir->t)); UNUSED(ir);
#else
  if (r >= RID_MAX_GPR)
    emit_vlso(as, irt_isnum(ir->t) ? ARMI_VSTR_D : ARMI_VSTR_S, r, RID_SP, ofs);
  else
#endif
    emit_lso(as, ARMI_STR, r, RID_SP, ofs);
}

/* Emit an arithmetic/logic operation with a constant operand. */
static void emit_opk(ASMState *as, ARMIns ai, Reg dest, Reg src,
		     int32_t i, RegSet allow)
{
  uint32_t k = emit_isk12(ai, i);
  if (k)
    emit_dn(as, ARMY_OP_BODY(ai, k), dest, src);
  else
    emit_dnm(as, ai, dest, src, ra_allock(as, i, allow));
}

/* Add offset to pointer. */
static void emit_addptr(ASMState *as, Reg r, int32_t ofs)
{
  if (ofs)
    emit_opk(as, ARMI_ADD, r, r, ofs, rset_exclude(RSET_GPR, r));
}

#define emit_spsub(as, ofs)	emit_addptr(as, RID_SP, -(ofs))

