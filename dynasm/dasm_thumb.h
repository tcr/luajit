/*
** DynASM ARM encoding engine.
** Copyright (C) 2005-2013 Mike Pall. All rights reserved.
** Released under the MIT license. See dynasm.lua for full copyright notice.
*/

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define DASM_ARCH "thumb"

#ifndef DASM_EXTERN
#define DASM_EXTERN(a, b, c, d) 0
#endif

/* Action definitions. */
enum {
  DASM_STOP,
  DASM_SECTION,
  DASM_ESC,
  DASM_REL_EXT,
  /* The following actions need a buffer position. */
  DASM_ALIGN,
  DASM_REL_LG,
  DASM_LABEL_LG,
  /* The following actions also have an argument. */
  DASM_REL_PC,
  DASM_LABEL_PC,
  DASM_IMM,
  DASM_IMMTHUMB,
  DASM_IMMLONG,
  DASM_IMMSHIFT,
  /* Finished. */
  DASM__MAX
};

/* Maximum number of section buffer positions for a single dasm_put() call. */
#define DASM_MAXSECPOS 25

/* DynASM encoder status codes. Action list offset or number are or'ed in. */
#define DASM_S_OK 0x00000000
#define DASM_S_NOMEM 0x01000000
#define DASM_S_PHASE 0x02000000
#define DASM_S_MATCH_SEC 0x03000000
#define DASM_S_RANGE_I 0x11000000
#define DASM_S_RANGE_SEC 0x12000000
#define DASM_S_RANGE_LG 0x13000000
#define DASM_S_RANGE_PC 0x14000000
#define DASM_S_RANGE_REL 0x15000000
#define DASM_S_UNDEF_LG 0x21000000
#define DASM_S_UNDEF_PC 0x22000000

/* Macros to convert positions (8 bit section + 24 bit index). */
#define DASM_POS2IDX(pos) ((pos) & 0x00ffffff)
#define DASM_POS2BIAS(pos) ((pos) & 0xff000000)
#define DASM_SEC2POS(sec) ((sec) << 24)
#define DASM_POS2SEC(pos) ((pos) >> 24)
#define DASM_POS2PTR(D, pos) (D->sections[DASM_POS2SEC(pos)].rbuf + (pos))

/* Action list type. */
typedef const uint16_t *dasm_ActList;

/* Per-section structure. */
typedef struct dasm_Section {
  int *rbuf;    /* Biased buffer pointer (negative section bias). */
  int *buf;     /* True buffer pointer. */
  size_t bsize; /* Buffer size in bytes. */
  int pos;      /* Biased buffer position. */
  int epos;     /* End of biased buffer position - max single put. */
  int ofs;      /* Byte offset into section. */
} dasm_Section;

/* Core structure holding the DynASM encoding state. */
struct dasm_State {
  size_t psize;            /* Allocated size of this structure. */
  dasm_ActList actionlist; /* Current actionlist pointer. */
  int *lglabels;           /* Local/global chain/pos ptrs. */
  size_t lgsize;
  int *pclabels; /* PC label chains/pos ptrs. */
  size_t pcsize;
  void **globals;           /* Array of globals (bias -10). */
  dasm_Section *section;    /* Pointer to active section. */
  size_t codesize;          /* Total size of all code sections. */
  int maxsection;           /* 0 <= sectionidx < maxsection. */
  int status;               /* Status code. */
  dasm_Section sections[1]; /* All sections. Alloc-extended. */
};

/* The size of the core structure depends on the max. number of sections. */
#define DASM_PSZ(ms) (sizeof(dasm_State) + (ms - 1) * sizeof(dasm_Section))

/* Initialize DynASM state. */
void dasm_init(Dst_DECL, int maxsection) {
  dasm_State *D;
  size_t psz = 0;
  int i;
  Dst_REF = NULL;
  DASM_M_GROW(Dst, struct dasm_State, Dst_REF, psz, DASM_PSZ(maxsection));
  D = Dst_REF;
  D->psize = psz;
  D->lglabels = NULL;
  D->lgsize = 0;
  D->pclabels = NULL;
  D->pcsize = 0;
  D->globals = NULL;
  D->maxsection = maxsection;
  for (i = 0; i < maxsection; i++) {
    D->sections[i].buf = NULL; /* Need this for pass3. */
    D->sections[i].rbuf = D->sections[i].buf - DASM_SEC2POS(i);
    D->sections[i].bsize = 0;
    D->sections[i].epos = 0; /* Wrong, but is recalculated after resize. */
  }
}

/* Free DynASM state. */
void dasm_free(Dst_DECL) {
  dasm_State *D = Dst_REF;
  int i;
  for (i = 0; i < D->maxsection; i++)
    if (D->sections[i].buf)
      DASM_M_FREE(Dst, D->sections[i].buf, D->sections[i].bsize);
  if (D->pclabels)
    DASM_M_FREE(Dst, D->pclabels, D->pcsize);
  if (D->lglabels)
    DASM_M_FREE(Dst, D->lglabels, D->lgsize);
  DASM_M_FREE(Dst, D, D->psize);
}

/* Setup global label array. Must be called before dasm_setup(). */
void dasm_setupglobal(Dst_DECL, void **gl, unsigned int maxgl) {
  dasm_State *D = Dst_REF;
  D->globals = gl - 10; /* Negative bias to compensate for locals. */
  DASM_M_GROW(Dst, int, D->lglabels, D->lgsize, (10 + maxgl) * sizeof(int));
}

/* Grow PC label array. Can be called after dasm_setup(), too. */
void dasm_growpc(Dst_DECL, unsigned int maxpc) {
  dasm_State *D = Dst_REF;
  size_t osz = D->pcsize;
  DASM_M_GROW(Dst, int, D->pclabels, D->pcsize, maxpc * sizeof(int));
  memset((void *)(((unsigned char *)D->pclabels) + osz), 0, D->pcsize - osz);
}

/* Setup encoder. */
void dasm_setup(Dst_DECL, const void *actionlist) {
  dasm_State *D = Dst_REF;
  int i;
  D->actionlist = (dasm_ActList)actionlist;
  D->status = DASM_S_OK;
  D->section = &D->sections[0];
  memset((void *)D->lglabels, 0, D->lgsize);
  if (D->pclabels)
    memset((void *)D->pclabels, 0, D->pcsize);
  for (i = 0; i < D->maxsection; i++) {
    D->sections[i].pos = DASM_SEC2POS(i);
    D->sections[i].ofs = 0;
  }
}

#ifdef DASM_CHECKS
#define CK(x, st)                                                              \
  do {                                                                         \
    if (!(x)) {                                                                \
      D->status = DASM_S_##st | (p - D->actionlist - 1);                       \
      if (D->status != DASM_S_OK) {                                            \
        fprintf(stderr,                                                        \
                "CK ERROR: line %d (n %x ins %x) start %d arg (%d)\n",         \
                __LINE__, n, ins, start, ap_count);                            \
      }                                                                        \
      return;                                                                  \
    }                                                                          \
  } while (0)
#define CKPL(kind, st)                                                         \
  do {                                                                         \
    if ((size_t)((char *)pl - (char *)D->kind##labels) >= D->kind##size) {     \
      D->status = DASM_S_RANGE_##st | (p - D->actionlist - 1);                 \
      if (D->status != DASM_S_OK) {                                            \
        fprintf(stderr, "CKPL ERROR: index %ld size %ld @ line %d\n",          \
                (size_t)((char *)pl - (char *)D->kind##labels), D->kind##size, \
                __LINE__);                                                     \
      }                                                                        \
      return;                                                                  \
    }                                                                          \
  } while (0)
#else
#define CK(x, st) ((void)0)
#define CKPL(kind, st) ((void)0)
#endif

static int32_t dasm_immthumb(int val) {
  if (val < 0) {
    return -1;
  }

  // fun encoding time!
  int a = (val & 0x80) >> 7;
  int abcdefgh = (val & 0xFF);
  int ABCDE = 00000;
  int i = 0;

  // Table A5-11 in ARM handbook
  if (val == abcdefgh) {
    // 00000000 00000000 00000000 abcdefgh
    ABCDE = 0 + a;
  } else if (val == ((abcdefgh << 16) | abcdefgh)) {
    // 00000000 abcdefgh 00000000 abcdefgh
    ABCDE = 2 + a;
  } else if (val == ((abcdefgh << 24) | (abcdefgh << 8))) {
    // abcdefgh 00000000 abcdefgh 00000000
    ABCDE = 4 + a;
  } else if (val == ((abcdefgh << 24) | (abcdefgh << 16) | (abcdefgh << 8) |
                     abcdefgh)) {
    // abcdefgh abcdefgh abcdefgh abcdefgh
    ABCDE = 6 + a;
  } else {
    // 1bcdefgh 00000000 00000000 00000000
    // ...
    // 00000000 00000000 00000001 bcdefgh0
    uint32_t truncval = val;
    for (i = 24; i >= 0; i--) {
      if ((truncval & 0x80) && ((truncval & 0xFF) == truncval)) {
        ABCDE = i + 8;
        truncval = truncval & 0x7f;
        break;
      }
      truncval >>= 1;
    }
    if (i == 0) {
      return -1;
    }
    if ((truncval + 0x80) << (32-ABCDE) != val) {
      return -1;
    }
    val = truncval;
  }
  return (ABCDE << 7) | (val & 0x7F);
}

#define DASM_IMM_SIGNED(x) ((x >> 10) & ((1 << 2) - 1))
#define DASM_IMM_BITS(x) ((x >> 5) & ((1 << 5) - 1))
#define DASM_IMM_SHIFT(x) ((x >> 1) & ((1 << 4) - 1))
#define DASM_IMM_SCALE(x) ((x >> 0) & 0x1 ? 2 : 0)

/* Pass 1: Store actions and args, link branches/labels, estimate offsets. */
void dasm_put(Dst_DECL, int start, ...) {
  va_list ap;
  int ap_count = 0;
  dasm_State *D = Dst_REF;
  dasm_ActList p = D->actionlist + start;
  dasm_Section *sec = D->section;
  int pos = sec->pos, ofs = sec->ofs;
  int *b;

  if (pos >= sec->epos) {
    DASM_M_GROW(Dst, int, sec->buf, sec->bsize,
                sec->bsize + 2 * DASM_MAXSECPOS * sizeof(int));
    sec->rbuf = sec->buf - DASM_POS2BIAS(pos);
    sec->epos =
        (int)sec->bsize / sizeof(int) - DASM_MAXSECPOS + DASM_POS2BIAS(pos);
  }

  b = sec->rbuf;
  b[pos++] = start;

  va_start(ap, start);
  while (1) {
    uint16_t ins = *p++;
    // unsigned int action = ((ins >> 4) & 0xf);
    // if ((action & 0xff00) != 0xff00) {

    // ins = action value
    // n = value as argument
    if (ins != 0xffff) {
      // unsigned int action = (ins >> 16);
      // if (action >= DASM__MAX) {
      ofs += 2;
    } else {
      ins = *p++;
      unsigned int action = (ins >> 12);
      if (action >= DASM_REL_PC) {
        ap_count += 1;
      }
      int *pl, n = action >= DASM_REL_PC ? va_arg(ap, int) : 0;
      switch (action) {
      case DASM_STOP:
        goto stop;
      case DASM_SECTION:
        n = (ins & 255);
        CK(n < D->maxsection, RANGE_SEC);
        D->section = &D->sections[n];
        goto stop;
      case DASM_ESC:
        p++;
        ofs += 2;
        break;
      case DASM_REL_EXT:
        break;
      case DASM_ALIGN:
        ofs += (ins & 255);
        b[pos++] = ofs;
        break;
      case DASM_REL_LG:
        // 0000 0111 1111 1111
        n = (ins & 2047) - 10;
        pl = D->lglabels + n;
        /* Bkwd rel or global. */
        if (n >= 0) {
          CK(n >= 10 || *pl < 0, RANGE_LG);
          CKPL(lg, LG);
          goto putrel;
        }
        pl += 10;
        n = *pl;
        // printf("DASM_REL_LG %x\n", n);
        if (n < 0)
          n = 0; /* Start new chain for fwd rel if label exists. */
        goto linkrel;
      case DASM_REL_PC:
        pl = D->pclabels + n;
        CKPL(pc, PC);
      putrel:
        n = *pl;
        if (n < 0) { /* Label exists. Get label pos and store it. */
          b[pos] = -n;
        } else {
        linkrel:
          b[pos] = n; /* Else link to rel chain, anchored at label. */
          *pl = pos;
        }
        pos++;
        break;
      case DASM_LABEL_LG:
        pl = D->lglabels + (ins & 2047) - 10;
        CKPL(lg, LG);
        goto putlabel;
      case DASM_LABEL_PC:
        pl = D->pclabels + n;
        CKPL(pc, PC);
      putlabel:
        n = *pl; /* n > 0: Collapse rel chain and replace with label pos. */
        while (n > 0) {
          int *pb = DASM_POS2PTR(D, n);
          n = *pb;
          *pb = pos;
        }
        *pl = -pos;     /* Label exists now. */
        b[pos++] = ofs; /* Store pass1 offset estimate. */
        break;
      case DASM_IMM:
#ifdef DASM_CHECKS
        // If n (value) does not fit in desired integer, abort.
        if (DASM_IMM_SIGNED(ins) && n < 0) {
          CK(((0 - n) & ((1 << DASM_IMM_SCALE(ins)) - 1)) == 0,
             RANGE_I);                                       // test scale
          CK(((0 - n) >> DASM_IMM_BITS(ins)) == 0, RANGE_I); // test bits
        } else {
          CK((n & ((1 << DASM_IMM_SCALE(ins)) - 1)) == 0,
             RANGE_I); // test scale
          CK(((n >> DASM_IMM_SCALE(ins)) >> DASM_IMM_BITS(ins)) == 0,
             RANGE_I); // test bits
        }
#endif
        b[pos++] = n;
        break;
      case DASM_IMMSHIFT:
        CK(n >= 0 && n < 32, RANGE_I);
        b[pos++] = n;
        break;
      case DASM_IMMLONG:
        // TODO IMM13
        CK(n > 0, RANGE_I);
        // CK(dasm_immthumb((unsigned int)n) != -1, RANGE_I);
        b[pos++] = n;
        break;
      case DASM_IMMTHUMB:
        CK(dasm_immthumb(n) != -1, RANGE_I);
        b[pos++] = n;
        break;
      }
    }
  }
stop:
  va_end(ap);
  sec->pos = pos;
  sec->ofs = ofs;
  // printf("OFS %d %p %p\n", ofs, D->actionlist + start, p);
}
#undef CK

/* Pass 2: Link sections, shrink aligns, fix label offsets. */
int dasm_link(Dst_DECL, size_t *szp) {
  dasm_State *D = Dst_REF;
  int secnum;
  int ofs = 0;

#ifdef DASM_CHECKS
  *szp = 0;
  if (D->status != DASM_S_OK)
    return D->status;
  {
    int pc;
    for (pc = 0; pc * sizeof(int) < D->pcsize; pc++)
      if (D->pclabels[pc] > 0)
        return DASM_S_UNDEF_PC | pc;
  }
#endif

  { /* Handle globals not defined in this translation unit. */
    int idx;
    for (idx = 20; idx * sizeof(int) < D->lgsize; idx++) {
      int n = D->lglabels[idx];
      /* Undefined label: Collapse rel chain and replace with marker (< 0). */
      while (n > 0) {
        int *pb = DASM_POS2PTR(D, n);
        n = *pb;
        *pb = -idx;
      }
    }
  }

  /* Combine all code sections. No support for data sections (yet). */
  for (secnum = 0; secnum < D->maxsection; secnum++) {
    dasm_Section *sec = D->sections + secnum;
    int *b = sec->rbuf;
    int pos = DASM_SEC2POS(secnum);
    int lastpos = sec->pos;

    while (pos != lastpos) {
      dasm_ActList p = D->actionlist + b[pos++];
      while (1) {
        uint16_t ins = *p++;
        if (ins == 0xffff) {
          ins = *p++;
          // printf("--> %x\n", p);
          unsigned int action = (ins >> 12);
          switch (action) {
          case DASM_STOP:
          case DASM_SECTION:
            goto stop;
          case DASM_ESC:
            p++;
            break;
          case DASM_REL_EXT:
            break;
          case DASM_ALIGN:
            ofs -= (b[pos++] + ofs) & (ins & 255);
            break;
          case DASM_REL_LG:
          case DASM_REL_PC:
            pos++;
            break;
          case DASM_LABEL_LG:
          case DASM_LABEL_PC:
            b[pos++] += ofs;
            break;
          case DASM_IMM:
          case DASM_IMMTHUMB:
          case DASM_IMMLONG:
          case DASM_IMMSHIFT:
            pos++;
            break;
          }
        }
      }
    stop:
      (void)0;
    }
    ofs += sec->ofs; /* Next section starts right after current section. */
  }

  D->codesize = ofs; /* Total size of all code sections */
  *szp = ofs;
  return DASM_S_OK;
}

#ifdef DASM_CHECKS
#define CK(x, st)                                                              \
  do {                                                                         \
    if (!(x))                                                                  \
      return DASM_S_##st | (p - D->actionlist - 1);                            \
  } while (0)
#else
#define CK(x, st) ((void)0)
#endif

/* Pass 3: Encode sections. */
int dasm_encode(Dst_DECL, void *buffer) {
  dasm_State *D = Dst_REF;
  char *base = (char *)buffer;
  uint16_t *cp = (uint16_t *)buffer;
  int secnum;
  uint16_t thumbexp;

  /* Encode all code sections. No support for data sections (yet). */
  for (secnum = 0; secnum < D->maxsection; secnum++) {
    dasm_Section *sec = D->sections + secnum;
    int *b = sec->buf;
    int *endb = sec->rbuf + sec->pos;

    while (b != endb) {
      dasm_ActList p = D->actionlist + *b++;
      while (1) {
        uint16_t ins = *p++;
        if (ins == 0xffff) {
          ins = *p++;
          // printf("--> %x\n", p);
          unsigned int action = (ins >> 12);
          int n = (action >= DASM_ALIGN && action < DASM__MAX) ? *b++ : 0;
          switch (action) {
          case DASM_STOP:
          case DASM_SECTION:
            goto stop;
          case DASM_ESC:
            *cp++ = *p++;
            break;
          case DASM_REL_EXT:
            n = DASM_EXTERN(Dst, (unsigned char *)cp, (ins & 2047),
                            !(ins & 2048));
            goto patchrel;
          case DASM_ALIGN:
            ins &= 255;
            while ((((char *)cp - base) & ins))
              *cp++ = 0xbf00;
            // TODO
            break;
          case DASM_REL_LG:
            CK(n >= 0, UNDEF_LG);
          case DASM_REL_PC:
            CK(n >= 0, UNDEF_PC);
            // printf("DASM_REL_PC ---> %x %x\n", ins, n);
            n = *DASM_POS2PTR(D, n) - (int)((char *)cp - base) - 4;
          patchrel:
            // printf("DASM_REL_PC -> %x %x\n", ins & 0x800, n);

            // (single-world "b", but not double-word "bl"), or "ldrd", or "ldr"
            if (((cp[-1] & 0xf000) == 0xd000 &&
                 ((cp[-2] & 0xf000) != 0xf000)) ||
                ((cp[-2] & 0xfe00) == 0xe800) ||
                ((cp[-1] & 0xf800) == 0x4800)) {
              // 1101[4:cond][8:imm]
              CK((n & 1) == 0 && -256 <= n && n <= 254, RANGE_REL);
              cp[-1] |= ((n >> 1) & 0x000000ff) + 1;
              // goto patchimml8;
            } else if (((cp[-1] & 0xf800) == 0x4800)) {
              // ldr 01001[3:Rt][8:imm]
              CK((n & 3) == 0 && -256 <= n && n <= 254, RANGE_REL);
              cp[-1] |= ((n >> 3) & 0x000000ff) + 1;
              // goto patchimml8;
            } else if ((cp[-1] & 0xf800) == 0xe000) {
              // 11100[11:imm]
              CK((n & 1) == 0 && -2048 <= n && n <= 2046, RANGE_REL);
              cp[-1] |= ((n >> 1) & 0x000007ff) + 1;
            } else if ((cp[-2] & 0xf800) == 0xf000) {
              // 11110[1:S][4:cond][6:imm] 10[1:J]0[1:J][11:imm]
              CK((n & 1) == 0 && -1048576 <= n && n <= 1048574, RANGE_REL);

              if (cp[-1] & (1 << 12)) { // 10-bit, no conditional
                cp[-2] &= ~((1 << 10) | 0x3ff);
              } else { // 6-bit w/ conditional
                cp[-2] &= ~((1 << 10) | 0x3f);
              }
              cp[-1] &= ~((1 << 13) | (1 << 11) | 0x7ff);

              uint32_t offset = (n >> 1) + 2;
              uint32_t S  = (offset & 0x800000) >> 23;
              uint32_t J1 = (offset & 0x400000) >> 22;
              uint32_t J2 = (offset & 0x200000) >> 21;
              if (cp[-1] & (1 << 12)) {
                J1 = (~J1 & 0x1);
                J2 = (~J2 & 0x1);
                J1 ^= S;
                J2 ^= S;
              }

              if (cp[-1] & (1 << 12)) { // 10-bit, no conditional
                cp[-2] |= (S << 10) | ((offset >> 11) & 0x3ff);
              } else { // 6-bit w/ conditional
                cp[-2] |= (S << 10) | ((offset >> 11) & 0x3f);
              }
              cp[-1] |= (J1 << 13) | (J2 << 11) | (offset & 0x7ff);
              // goto patchimml;
            } else {
              printf("Invalid branch opcode (%x) %x at %d\n", cp[-2], cp[-1],
                     p - D->actionlist);
              CK(0, RANGE_REL);
            }
            break;
          case DASM_LABEL_LG:
            ins &= 2047;
            if (ins >= 20)
              D->globals[ins - 10] = (void *)(base + n);
            break;
          case DASM_LABEL_PC:
            break;
          case DASM_IMM:
            // 'U' in PUW
            if (DASM_IMM_SIGNED(ins)) {
              if (n < 0) {
                n = -n;
              } else {
                if (DASM_IMM_SIGNED(ins) == 2) {
                  cp[-2] |= 1 << 7;
                } else if (DASM_IMM_SIGNED(ins) == 1) {
                  cp[-1] |= 1 << 9;
                }
              }
            }

            cp[-1] |= ((n >> (DASM_IMM_SCALE(ins))) &
                       ((1 << (DASM_IMM_BITS(ins))) - 1))
                      << (DASM_IMM_SHIFT(ins));
            break;
          case DASM_IMMLONG:
            cp[-2] |= ((n >> 11) & 0x1) << 10;
            cp[-1] |= ((n >> 8) & 0x7) << 12;
            cp[-1] |= n & 0xFF;
            break;
          case DASM_IMMTHUMB:
            thumbexp = dasm_immthumb(n);
            cp[-2] |= ((thumbexp >> 11) & 0x1) << 10;
            cp[-1] |= ((thumbexp >> 8) & 0x7) << 12;
            cp[-1] |= thumbexp & 0xFF;
            break;
          case DASM_IMMSHIFT:
            cp[-1] |= (((n >> 2) & 0x7) << 12) | ((n & 0x3) << 6);
            break;
          }
        } else {
          *cp++ = ins;
        }
      }
    stop:
      (void)0;
    }
  }

  if (base + D->codesize != (char *)cp) /* Check for phase errors. */
    return DASM_S_PHASE;
  return DASM_S_OK;
}
#undef CK

/* Get PC label offset. */
int dasm_getpclabel(Dst_DECL, unsigned int pc) {
  dasm_State *D = Dst_REF;
  if (pc * sizeof(int) < D->pcsize) {
    int pos = D->pclabels[pc];
    if (pos < 0)
      return *DASM_POS2PTR(D, -pos);
    if (pos > 0)
      return -1; /* Undefined. */
  }
  return -2; /* Unused or out of range. */
}

#ifdef DASM_CHECKS
/* Optional sanity checker to call between isolated encoding steps. */
int dasm_checkstep(Dst_DECL, int secmatch) {
  dasm_State *D = Dst_REF;
  if (D->status == DASM_S_OK) {
    int i;
    for (i = 1; i <= 9; i++) {
      if (D->lglabels[i] > 0) {
        D->status = DASM_S_UNDEF_LG | i;
        break;
      }
      D->lglabels[i] = 0;
    }
  }
  if (D->status == DASM_S_OK && secmatch >= 0 &&
      D->section != &D->sections[secmatch])
    D->status = DASM_S_MATCH_SEC | (D->section - D->sections);
  return D->status;
}
#endif
