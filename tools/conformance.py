#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""V810 semantics conformance: fixed encodings, no ROM required.

Every other check in this repository needs the ROMs you supplied. `sweep.ps1`
recompiles them, `render_survey.ps1` boots them and looks at the framebuffer.
Both are the right tests and neither can run on a clean checkout, which means a
change to the emitter cannot be checked by anyone who does not already own the
library -- and neither one can tell you *which instruction* was wrong when a
game stalls. That is the gap this fills.

The encodings below are written down rather than harvested from a game, chosen
to pin the semantics V810 gets wrong quietly -- the ones that produce a
plausible number instead of a crash, so a game drifts into a state its main
loop never leaves and the screen simply stays black. Each case is assembled
into a synthetic ROM, recompiled by the real `v810recomp`, compiled by MSVC and
executed against the real `vbrecomp` runtime, so a pass here means the same
thing a pass on a real game does: this path, end to end, is what runs.

    python tools/conformance.py             # run them
    python tools/conformance.py --list      # show what each encoding decodes to
    python tools/conformance.py -k mul      # only cases whose name matches

Expected values come from the V810 architecture manual, not from our own
runtime -- a case that merely restated what `cpu.h` already does would pass
forever and pin nothing.

What it deliberately does not cover: control flow beyond a single function
(each case runs one straight-line body and returns), the VIP/VSU/timer
hardware, and interrupt delivery. Those are what `render_survey.ps1` exercises
on real games.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 64 KB is the smallest size that still holds the interrupt vector table at its
# hardware address (0xFFFFFE00 mirrors to ROM offset 0xFE00), so the synthetic
# ROM goes through exactly the same header/vector path a real one does.
ROM_SIZE = 0x10000
ROM_BASE = 0x08000000 - ROM_SIZE

# Each case body gets its own 64-byte slot from the start of ROM. Nothing here
# needs more than a handful of instructions, and a fixed stride keeps the
# address of case N derivable on both sides.
SLOT = 0x40

PSW_BITS = {"Z": 1 << 0, "S": 1 << 1, "OV": 1 << 2, "CY": 1 << 3}

# The host exceptions a wrong translation actually produces. An instruction the
# V810 completes must not take the process down.
EXCEPTIONS = {
    0xC0000094: "integer divide by zero (host trap)",
    0xC0000095: "integer overflow (host idiv trap)",
    0xC0000005: "access violation",
    0xC000008E: "float divide by zero",
    0xC0000090: "float invalid operation",
}


# --------------------------------------------------------------------------
# Encoders. One per V810 instruction format; the case table calls these rather
# than spelling out hex, because a wrong hex digit is invisible and a wrong
# register number in `f1(0x01, 7, 6)` is not. `--list` prints the decoder's own
# disassembly next to the case name, which is how a mistake here gets caught
# rather than silently testing a different instruction.
# --------------------------------------------------------------------------

def f1(op, reg2, reg1):
    """Format I: reg1, reg2 (16-bit)."""
    return [(op << 10) | (reg2 << 5) | reg1]


def f2(op, reg2, imm5):
    """Format II: imm5, reg2 (16-bit). imm5 sits in the reg1 field."""
    return [(op << 10) | (reg2 << 5) | (imm5 & 0x1F)]


def f5(op, reg2, reg1, imm16):
    """Format V: imm16, reg1, reg2 (32-bit)."""
    return [(op << 10) | (reg2 << 5) | reg1, imm16 & 0xFFFF]


def f6(op, reg2, reg1, disp16):
    """Format VI: disp16[reg1], reg2 (32-bit load/store)."""
    return f5(op, reg2, reg1, disp16)


def f7(subop, reg2, reg1):
    """Format VII: subopcode (32-bit float / bit ops)."""
    return [(0x3E << 10) | (reg2 << 5) | reg1, (subop << 10)]


RET = f1(0x06, 0, 31)   # jmp [r31] -- the emitter turns this into `return`


def Case(name, code, setup, expect, why):
    """One conformance case.

    setup / expect are {register: value}; expect may also carry "psw", a
    space-separated set of the Z/S/OV/CY flags that must be SET -- every flag
    not named must be CLEAR, so a case pins the flags it does not mention just
    as tightly as the ones it does. Omit "psw" to ignore flags entirely.
    """
    return {"name": name, "code": code, "setup": setup, "expect": expect,
            "why": why}


CASES = [
    # ------------------------------------------------------------- add / sub
    # The flag a branch reads two instructions later. Getting CY and OV
    # backwards is invisible in the arithmetic and decides every comparison.
    Case("add: carry out", f1(0x01, 7, 6), {6: 1, 7: 0xFFFFFFFF},
         {7: 0, "psw": "Z CY"}, "unsigned wrap sets CY, and the result is zero"),
    Case("add: signed overflow", f1(0x01, 7, 6), {6: 1, 7: 0x7FFFFFFF},
         {7: 0x80000000, "psw": "S OV"}, "OV is signed, CY is not; only OV here"),
    Case("sub: borrow", f1(0x02, 7, 6), {6: 5, 7: 3},
         {7: 0xFFFFFFFE, "psw": "S CY"}, "CY is a borrow: set when b > a"),
    Case("sub: no borrow, equal", f1(0x02, 7, 6), {6: 7, 7: 7},
         {7: 0, "psw": "Z"}, "equal operands: Z only, CY clear"),
    Case("sub: signed overflow", f1(0x02, 7, 6), {6: 1, 7: 0x80000000},
         {7: 0x7FFFFFFF, "psw": "OV"}, "INT_MIN - 1 wraps positive, OV not CY"),
    Case("cmp: keeps only flags", f1(0x03, 7, 6), {6: 9, 7: 4},
         {7: 4, "psw": "S CY"}, "cmp must not write reg2"),
    Case("addi: sign-extended imm16", f5(0x29, 7, 6, 0xFFFF), {6: 10, 7: 0},
         {7: 9, "psw": "CY"}, "imm16 is signed: +0xFFFF is -1"),
    Case("add imm5: sign-extended", f2(0x11, 7, 0x1F), {7: 10},
         {7: 9, "psw": "CY"}, "imm5 is signed: 0x1F is -1, not 31"),
    Case("cmp imm5: sign-extended", f2(0x13, 7, 0x1F), {7: 0xFFFFFFFF},
         {7: 0xFFFFFFFF, "psw": "Z"}, "-1 == -1 via a 5-bit immediate"),
    Case("mov imm5: sign-extended", f2(0x10, 7, 0x10), {7: 0},
         {7: 0xFFFFFFF0}, "0x10 as imm5 is -16, and mov sets no flags"),

    # ------------------------------------------------------------- logic
    # OV is cleared and CY is left alone. A logic op that clobbers CY breaks
    # the multi-word arithmetic that follows it.
    Case("and: clears OV, keeps CY", f1(0x0D, 7, 6), {6: 0xF0F0, 7: 0xFF00},
         {7: 0xF000, "psw": ""}, "Z/S from result, OV cleared, CY untouched"),
    Case("or: sets S from bit 31", f1(0x0C, 7, 6), {6: 0x80000000, 7: 1},
         {7: 0x80000001, "psw": "S"}, "S is bit 31 of the result"),
    Case("xor: to zero", f1(0x0E, 7, 6), {6: 0x1234, 7: 0x1234},
         {7: 0, "psw": "Z"}, "Z when the result is zero"),
    Case("not: reg1 into reg2", f1(0x0F, 7, 6), {6: 0, 7: 0x1234},
         {7: 0xFFFFFFFF, "psw": "S"}, "not reads reg1, writes reg2"),
    Case("andi: zero-extended imm16", f5(0x2D, 7, 6, 0xFFFF), {6: 0xFFFFFFFF, 7: 0},
         {7: 0x0000FFFF, "psw": ""}, "logic immediates are zero-extended, unlike addi"),
    Case("ori: zero-extended imm16", f5(0x2C, 7, 6, 0x8000), {6: 0, 7: 0},
         {7: 0x00008000, "psw": ""}, "0x8000 must not sign-extend to 0xFFFF8000"),
    Case("xori: zero-extended imm16", f5(0x2E, 7, 6, 0xFFFF), {6: 0, 7: 0},
         {7: 0x0000FFFF, "psw": ""}, "same zero-extension for xori"),

    # ------------------------------------------------------------- shifts
    # The shift amount is masked to 5 bits and CY is the last bit shifted out.
    # A shift of zero is the case every naive implementation gets wrong.
    Case("shl: carry out", f1(0x04, 7, 6), {6: 1, 7: 0x80000000},
         {7: 0, "psw": "Z CY"}, "the bit shifted off the top lands in CY"),
    Case("shr: carry out", f1(0x05, 7, 6), {6: 1, 7: 3},
         {7: 1, "psw": "CY"}, "logical right shift, CY is the bit shifted off"),
    Case("sar: keeps sign", f1(0x07, 7, 6), {6: 4, 7: 0x80000000},
         {7: 0xF8000000, "psw": "S"}, "arithmetic shift replicates bit 31"),
    Case("shl: amount masked to 5 bits", f1(0x04, 7, 6), {6: 33, 7: 1},
         {7: 2, "psw": ""}, "33 & 31 == 1, not a 33-bit shift"),
    Case("shl: zero amount", f1(0x04, 7, 6), {6: 0, 7: 0xFFFFFFFF},
         {7: 0xFFFFFFFF, "psw": "S"}, "a shift of zero leaves the value and clears CY"),
    Case("shl imm5: count is unsigned", f2(0x14, 7, 0x1F), {7: 1},
         {7: 0x80000000, "psw": "S"}, "the imm5 shift count is 31, NOT sign-extended to -1"),
    Case("sar imm5: count is unsigned", f2(0x17, 7, 0x1F), {7: 0x80000000},
         {7: 0xFFFFFFFF, "psw": "S"},
         "the count is 31, NOT sign-extended to -1; the last bit out is bit 30"),

    # ------------------------------------------------------------- multiply
    # The result is 64 bits across r30:reg2, and the flags describe all 64 --
    # reading them off the low word alone reports Z for a nonzero product.
    Case("mul: 64-bit result", f1(0x08, 7, 6), {6: 0x10000, 7: 0x10000},
         {7: 0, 30: 1, "psw": "OV"},
         "low word zero but the product is not: Z must stay clear"),
    Case("mul: negative", f1(0x08, 7, 6), {6: 0xFFFFFFFF, 7: 3},
         {7: 0xFFFFFFFD, 30: 0xFFFFFFFF, "psw": "S"},
         "S is the sign of the whole 64-bit product; -3 fits, so no OV"),
    Case("mul: small positive", f1(0x08, 7, 6), {6: 6, 7: 7},
         {7: 42, 30: 0, "psw": ""}, "the ordinary case: no flags but the value"),
    Case("mulu: high word", f1(0x0A, 7, 6), {6: 0x10000, 7: 0x10000},
         {7: 0, 30: 1, "psw": "OV"}, "unsigned: 0x10000^2 needs the high word"),
    Case("mulu: no sign extension", f1(0x0A, 7, 6), {6: 0xFFFFFFFF, 7: 2},
         {7: 0xFFFFFFFE, 30: 1, "psw": "OV"},
         "0xFFFFFFFF is 4 billion here, not -1, so the product is positive "
         "even though its low word has bit 31 set"),
    Case("mpyhw: 16x16 signed", f7(0x0C, 7, 6), {6: 0xFFFF, 7: 3},
         {7: 0xFFFFFFFD}, "only the low halfwords, and they are signed: -1 * 3"),

    # ------------------------------------------------------------- divide
    # Division is where a wrong answer becomes a crashed process: on x86,
    # INT_MIN / -1 raises rather than wrapping, so the host faults on an
    # instruction the V810 completes.
    Case("div: quotient and remainder", f1(0x09, 7, 6), {6: 3, 7: 17},
         {7: 5, 30: 2, "psw": ""}, "quotient in reg2, remainder in r30"),
    Case("div: negative truncates toward zero", f1(0x09, 7, 6),
         {6: 3, 7: 0xFFFFFFF6}, {7: 0xFFFFFFFD, 30: 0xFFFFFFFF, "psw": "S"},
         "-10 / 3 is -3 remainder -1 truncating; flooring would give -4 rem 2"),
    Case("div: INT_MIN / -1", f1(0x09, 7, 6), {6: 0xFFFFFFFF, 7: 0x80000000},
         {7: 0x80000000, 30: 0, "psw": "S OV"},
         "the one case that faults on x86: V810 returns INT_MIN and sets OV"),
    Case("div: by zero leaves state alone", f1(0x09, 7, 6), {6: 0, 7: 1234},
         {7: 1234}, "no divide-by-zero exception path yet: must not crash"),
    Case("divu: unsigned", f1(0x0B, 7, 6), {6: 2, 7: 0xFFFFFFFF},
         {7: 0x7FFFFFFF, 30: 1, "psw": ""},
         "0xFFFFFFFF is 4 billion: signed division would give 0"),

    # ------------------------------------------------------------- setf
    # Every condition code, evaluated against flags a cmp just set. setf is how
    # a game turns a comparison into a value, so a wrong table entry silently
    # picks the wrong branch of a state machine.
    Case("setf: bz after equal cmp", [*f1(0x03, 7, 6), *f2(0x12, 8, 0x2)],
         {6: 4, 7: 4}, {8: 1}, "cond 2 (Z) is true after comparing equals"),
    Case("setf: bnz after equal cmp", [*f1(0x03, 7, 6), *f2(0x12, 8, 0xA)],
         {6: 4, 7: 4}, {8: 0}, "cond A (NZ) is the inverse of Z"),
    Case("setf: bl unsigned less", [*f1(0x03, 7, 6), *f2(0x12, 8, 0x1)],
         {6: 0xFFFFFFFF, 7: 1}, {8: 1},
         "cond 1 (CY) is UNSIGNED less-than: 1 < 4294967295"),
    Case("setf: blt signed less", [*f1(0x03, 7, 6), *f2(0x12, 8, 0x6)],
         {6: 0xFFFFFFFF, 7: 1}, {8: 0},
         "cond 6 (S^OV) is SIGNED less-than: 1 < -1 is false"),
    Case("setf: bgt signed greater", [*f1(0x03, 7, 6), *f2(0x12, 8, 0xF)],
         {6: 0xFFFFFFFF, 7: 1}, {8: 1}, "1 > -1 signed"),
    Case("setf: bnh unsigned <=", [*f1(0x03, 7, 6), *f2(0x12, 8, 0x3)],
         {6: 4, 7: 4}, {8: 1}, "cond 3 is CY|Z: equal counts as not-higher"),
    Case("setf: bh unsigned >", [*f1(0x03, 7, 6), *f2(0x12, 8, 0xB)],
         {6: 4, 7: 4}, {8: 0}, "cond B excludes equality"),
    Case("setf: blt across overflow", [*f1(0x03, 7, 6), *f2(0x12, 8, 0x6)],
         {6: 1, 7: 0x80000000}, {8: 1},
         "INT_MIN < 1: the subtract overflows, and S^OV is why signed "
         "comparison still works"),

    # ------------------------------------------------------------- moves
    Case("movea: sign-extended, no flags", f5(0x28, 7, 6, 0xFFFF),
         {6: 0x1000, 7: 0}, {7: 0x0FFF, "psw": ""},
         "movea adds a signed imm16 and touches no flag"),
    Case("movhi: shifts into the top", f5(0x2F, 7, 6, 0x0700),
         {6: 0, 7: 0}, {7: 0x07000000, "psw": ""},
         "the reset-vector idiom: movhi then movea builds a 32-bit address"),
    Case("mov: register to register", f1(0x00, 7, 6), {6: 0xDEADBEEF, 7: 0},
         {7: 0xDEADBEEF, "psw": ""}, "no flags"),
    Case("r0 stays zero", f1(0x00, 0, 6), {6: 0xFFFFFFFF},
         {0: 0}, "writes to r0 are discarded by hardware"),

    # ------------------------------------------------------------- byte ops
    Case("xb: swaps the low two bytes", f7(0x08, 7, 0),
         {7: 0x11223344}, {7: 0x11224433}, "only the low halfword is touched"),
    Case("xh: swaps halfwords", f7(0x09, 7, 0),
         {7: 0x11223344}, {7: 0x33441122}, "a 32-bit halfword rotate"),
    Case("rev: reverses all 32 bits", f7(0x0A, 7, 6),
         {6: 0x00000001, 7: 0}, {7: 0x80000000}, "bit 0 becomes bit 31"),

    # ------------------------------------------------------------- memory
    # Loads differ only in whether they sign-extend, and that is the whole
    # difference between reading a signed sprite offset and reading garbage.
    # WRAM at 0x05000000 is writable by the runtime, so a store then a load
    # exercises the real memory bus.
    Case("ld.b sign-extends",
         [*f5(0x2F, 6, 0, 0x0500),                    # movhi 0x0500, r0, r6
          *f2(0x10, 7, 0x0F),                         # mov 15, r7  (0x0F)
          *f6(0x34, 7, 6, 0),                         # st.b r7, 0[r6]
          *f2(0x10, 7, 0x1F),                         # mov -1, r7
          *f6(0x30, 7, 6, 0)],                        # ld.b 0[r6], r7
         {}, {7: 0x0000000F},
         "0x0F is positive: sign extension must leave it alone"),
    Case("ld.b sign-extends a negative byte",
         [*f5(0x2F, 6, 0, 0x0500),
          *f2(0x10, 7, 0x1F),                         # mov -1, r7 (0xFF byte)
          *f6(0x34, 7, 6, 4),
          *f6(0x30, 7, 6, 4)],
         {}, {7: 0xFFFFFFFF}, "a stored 0xFF reads back as -1, not 255"),
    Case("in.b zero-extends",
         [*f5(0x2F, 6, 0, 0x0500),
          *f2(0x10, 7, 0x1F),
          *f6(0x34, 7, 6, 8),
          *f6(0x38, 7, 6, 8)],                        # in.b 8[r6], r7
         {}, {7: 0x000000FF},
         "in.b is the same load without the sign extension"),
    Case("ld.h sign-extends",
         [*f5(0x2F, 6, 0, 0x0500),
          *f5(0x2F, 7, 0, 0xFFFF),                    # r7 = 0xFFFF0000
          *f5(0x28, 7, 7, 0x8000),                    # movea -> 0xFFFF8000
          *f6(0x35, 7, 6, 0x10),                      # st.h r7, 16[r6]
          *f6(0x31, 7, 6, 0x10)],                     # ld.h 16[r6], r7
         {}, {7: 0xFFFF8000}, "a stored 0x8000 halfword reads back negative"),
    Case("st.w / ld.w round-trip",
         [*f5(0x2F, 6, 0, 0x0500),
          *f5(0x2F, 7, 0, 0xDEAD),
          *f5(0x28, 7, 7, 0x1234),
          *f6(0x37, 7, 6, 0x20),
          *f2(0x10, 7, 0),
          *f6(0x33, 7, 6, 0x20)],
         {}, {7: 0xDEAD1234}, "the full word survives the bus"),
    Case("ld.w: negative displacement",
         [*f5(0x2F, 6, 0, 0x0500),
          *f5(0x28, 6, 6, 0x0100),                    # r6 = 0x05000100
          *f2(0x10, 7, 0x0A),
          *f6(0x37, 7, 6, 0xFFFC),                    # st.w r7, -4[r6]
          *f2(0x10, 7, 0),
          *f6(0x33, 7, 6, 0xFFFC)],                   # ld.w -4[r6], r7
         {}, {7: 10}, "disp16 is signed, so -4 addresses backwards"),

    # ------------------------------------------------------------- float
    # The V810's single-precision unit. Games use it for 3D and for the
    # fixed-point conversions around it; TRNC and CVT differ in rounding, which
    # is a one-unit error that accumulates into a wrong on-screen position.
    Case("cvt.ws: int to float",
         [*f2(0x10, 6, 0x0A), *f7(0x02, 7, 6)],
         {}, {7: 0x41200000}, "10.0f is 0x41200000"),
    Case("trnc.sw: truncates toward zero",
         [*f5(0x2F, 6, 0, 0x4004),                    # r6 = 2.5f (0x40200000)
          *f5(0x28, 6, 6, 0x0000),
          *f7(0x0B, 7, 6)],
         {}, {7: 2}, "2.5 truncates to 2"),
    Case("addf.s",
         [*f5(0x2F, 6, 0, 0x3F80),                    # 1.0f
          *f5(0x2F, 7, 0, 0x4000),                    # 2.0f
          *f7(0x04, 7, 6)],
         {}, {7: 0x40400000}, "1.0 + 2.0 = 3.0f"),
    Case("mulf.s",
         [*f5(0x2F, 6, 0, 0x4000),                    # 2.0f
          *f5(0x2F, 7, 0, 0x4080),                    # 4.0f
          *f7(0x06, 7, 6)],
         {}, {7: 0x41000000}, "2.0 * 4.0 = 8.0f"),

    # ------------------------------------------------------------- bitstring
    # movbsu is how a VB game blits: a bit-granular block copy whose operands
    # live in r26-r30 rather than in the encoding. It was a no-op for the whole
    # corpus until these cases existed, which is invisible from the outside --
    # the game runs on, having drawn nothing.
    #
    # Each case builds its own source word in WRAM, points the register block
    # at it, and reads back what landed at the destination.
    Case("movbsu: aligned word copy",
         [*f5(0x2F, 6, 0, 0x0500),                    # r6 = 0x05000000
          *f5(0x2F, 7, 0, 0xAAAA),                    # r7 = 0xAAAA5555
          *f5(0x28, 7, 7, 0x5555),
          *f6(0x37, 7, 6, 0x40),                      # [r6+0x40] = source
          *f5(0x28, 26, 6, 0x0040),                   # r26 = source address
          *f5(0x28, 27, 6, 0x0050),                   # r27 = dest address
          *f5(0x29, 28, 0, 32),                       # r28 = 32 bits
          *f2(0x10, 29, 0), *f2(0x10, 30, 0),         # both offsets 0
          *f2(0x1F, 0, 0x0B),                         # movbsu
          *f6(0x33, 7, 6, 0x50)],                     # r7 = [r6+0x50]
         {}, {7: 0xAAAA5555},
         "a whole word, offset zero: the case every other one is a shift of"),
    Case("movbsu: advances the registers",
         [*f5(0x2F, 6, 0, 0x0500),
          *f5(0x28, 26, 6, 0x0040),
          *f5(0x28, 27, 6, 0x0050),
          *f5(0x29, 28, 0, 32),
          *f2(0x10, 29, 0), *f2(0x10, 30, 0),
          *f2(0x1F, 0, 0x0B)],
         {}, {26: 0x05000044, 27: 0x05000054, 28: 0, 29: 0, 30: 0},
         "the instruction is restartable: it must leave the registers pointing "
         "past what it copied, or a continued copy repeats itself forever"),
    Case("movbsu: unaligned destination bit",
         [*f5(0x2F, 6, 0, 0x0500),
          *f2(0x10, 7, 0x0F),                         # r7 = 0x0F (4 bits set)
          *f6(0x37, 7, 6, 0x60),                      # source = 0x0000000F
          *f2(0x10, 7, 0),
          *f6(0x37, 7, 6, 0x70),                      # dest starts clear
          *f5(0x28, 26, 6, 0x0060),
          *f5(0x28, 27, 6, 0x0070),
          *f5(0x29, 28, 0, 4),                        # 4 bits
          *f5(0x29, 29, 0, 8),                        # dest bit offset 8
          *f2(0x10, 30, 0),                           # source bit offset 0
          *f2(0x1F, 0, 0x0B),
          *f6(0x33, 7, 6, 0x70)],
         {}, {7: 0x00000F00},
         "four bits landing at bit 8: the arbitrary-offset case that makes "
         "this a bit-string op and not a memcpy"),
    Case("movbsu: crosses a word boundary",
         [*f5(0x2F, 6, 0, 0x0500),
          *f2(0x10, 7, 0x1F),                         # r7 = -1, all bits set
          *f6(0x37, 7, 6, 0x80),                      # source word = all ones
          *f2(0x10, 7, 0),
          *f6(0x37, 7, 6, 0x90), *f6(0x37, 7, 6, 0x94),
          *f5(0x28, 26, 6, 0x0080),
          *f5(0x28, 27, 6, 0x0090),
          *f5(0x29, 28, 0, 8),                        # 8 bits
          *f5(0x29, 29, 0, 28),                       # starting at bit 28
          *f2(0x10, 30, 0),
          *f2(0x1F, 0, 0x0B),
          *f6(0x33, 7, 6, 0x94)],                     # read the SECOND word
         {}, {7: 0x0000000F},
         "8 bits from bit 28 spill 4 bits into the next word; dropping them "
         "is the silent half-blit that leaves a sprite torn"),
    Case("orbsu: combines with the destination",
         [*f5(0x2F, 6, 0, 0x0500),
          *f5(0x2F, 7, 0, 0x0000), *f5(0x28, 7, 7, 0x00FF),
          *f6(0x37, 7, 6, 0xA0),                      # source = 0x000000FF
          *f5(0x2F, 7, 0, 0x0000), *f5(0x28, 7, 7, 0x0F00),
          *f6(0x37, 7, 6, 0xB0),                      # dest   = 0x00000F00
          *f5(0x28, 26, 6, 0x00A0),
          *f5(0x28, 27, 6, 0x00B0),
          *f5(0x29, 28, 0, 32),
          *f2(0x10, 29, 0), *f2(0x10, 30, 0),
          *f2(0x1F, 0, 0x08),                         # orbsu
          *f6(0x33, 7, 6, 0xB0)],
         {}, {7: 0x00000FFF},
         "or, not overwrite: the destination bits survive"),
    Case("andnbsu: inverts the source",
         [*f5(0x2F, 6, 0, 0x0500),
          *f5(0x2F, 7, 0, 0x0000), *f5(0x28, 7, 7, 0x00FF),
          *f6(0x37, 7, 6, 0xC0),                      # source = 0x000000FF
          *f5(0x2F, 7, 0, 0x0000), *f5(0x28, 7, 7, 0x0FFF),
          *f6(0x37, 7, 6, 0xD0),                      # dest   = 0x00000FFF
          *f5(0x28, 26, 6, 0x00C0),
          *f5(0x28, 27, 6, 0x00D0),
          *f5(0x29, 28, 0, 32),
          *f2(0x10, 29, 0), *f2(0x10, 30, 0),
          *f2(0x1F, 0, 0x0D),                         # andnbsu
          *f6(0x33, 7, 6, 0xD0)],
         {}, {7: 0x00000F00},
         "the N forms invert the SOURCE bit, not the result: a mask-out"),

    # ------------------------------------------------------------- system
    Case("ldsr / stsr round-trip",
         [*f5(0x2F, 6, 0, 0x0001), *f2(0x1C, 6, 25), *f2(0x1D, 7, 25)],
         {}, {7: 0x00010000},
         "ldsr writes a system register, stsr reads it back (ADTRE, id 25)"),
    Case("stsr pir", f2(0x1D, 7, 6), {}, {7: 0x00005346},
         "the processor ID is a fixed constant games check for"),
]


# --------------------------------------------------------------------------
# Building the synthetic ROM
# --------------------------------------------------------------------------

def build_rom(cases):
    """Lay every case body out in ROM and return (image, {name: address}).

    Unused space is left as zero, which decodes as `mov r0, r0` -- harmless if
    analysis ever walks past a body, and it keeps the interrupt vectors reading
    as the "unused" filler the recompiler already skips.
    """
    rom = bytearray(ROM_SIZE)
    addrs = {}
    for i, case in enumerate(cases):
        off = i * SLOT
        if off + SLOT > 0xFE00:
            raise RuntimeError("too many cases for a 64 KB ROM")
        words = list(case["code"]) + RET
        if len(words) * 2 > SLOT:
            raise RuntimeError(f"{case['name']}: body exceeds the {SLOT}-byte slot")
        for j, hw in enumerate(words):
            struct.pack_into("<H", rom, off + j * 2, hw & 0xFFFF)
        addrs[case["name"]] = ROM_BASE + off

    # Reset vector (last 16 bytes, mapped to 0xFFFFFFF0): movhi/movea/jmp to
    # case 0. Nothing calls it -- the harness calls each body directly -- but
    # the recompiler decodes it to find the entry point, and a ROM without one
    # would take the "could not decode reset vector" fallback path instead of
    # the path every real ROM takes.
    hi, lo = (ROM_BASE >> 16) & 0xFFFF, ROM_BASE & 0xFFFF
    if lo & 0x8000:
        hi = (hi + 1) & 0xFFFF   # movea sign-extends; compensate in the high half
    vec = f5(0x2F, 31, 0, hi) + f5(0x28, 31, 31, lo) + f1(0x06, 0, 31)
    for j, hw in enumerate(vec):
        struct.pack_into("<H", rom, ROM_SIZE - 16 + j * 2, hw & 0xFFFF)

    # Header: 20-byte title at (size - 544), the layout print_header reads.
    rom[ROM_SIZE - 544:ROM_SIZE - 524] = b"V810 CONFORMANCE    "
    return bytes(rom), addrs


def harness_source(cases, addrs):
    """C that runs each body and prints the register file it left behind.

    Comparison happens in Python, against the table above; the C stays dumb so
    that adding a case never means touching two languages.
    """
    nl = "\n"
    out = ['#include <windows.h>', '#include <vbrecomp/vbrecomp.h>',
           '#include <stdio.h>', '#include <stdlib.h>', '#include <string.h>', '',
           '#include "recomp_funcs.h"', '',
           'static uint8_t rom[%d];' % ROM_SIZE, '',
           '/* Each body runs under SEH so that a case which faults on the host',
           ' * (an idiv overflow, a bad address) reports itself instead of taking',
           ' * the whole run down and hiding every case after it. A fault IS the',
           ' * finding: the V810 completes those instructions. */',
           '#define RUN(addr, fn) do { \\',
           '    int _i; \\',
           '    __try { fn(); } \\',
           '    __except (EXCEPTION_EXECUTE_HANDLER) { \\',
           '        printf("FAULT %08X %08X\\n", addr, GetExceptionCode()); \\',
           '        fflush(stdout); break; \\',
           '    } \\',
           '    printf("CASE %08X", addr); \\',
           '    for (_i = 0; _i < 32; _i++) printf(" %08X", vb_cpu.r[_i]); \\',
           '    printf(" PSW %08X\\n", vb_cpu.sr[VB_SREG_PSW]); \\',
           '    fflush(stdout); \\',
           '} while (0)', '',
           'int main(int argc, char **argv) {',
           '    FILE *f = fopen(argv[1], "rb");',
           '    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) '
           '{ fprintf(stderr, "rom?\\n"); return 2; }',
           '    fclose(f);',
           '    vb_init(rom, sizeof rom);',
           '']
    for case in cases:
        addr = addrs[case["name"]]
        out.append('    /* %s */' % case["name"])
        out.append('    memset(&vb_cpu, 0, sizeof vb_cpu);')
        out.append('    vb_cpu.sr[VB_SREG_PIR] = 0x00005346;')
        for reg, val in sorted(case["setup"].items()):
            out.append('    vb_cpu.r[%d] = 0x%08Xu;' % (reg, val))
        out.append('    RUN(0x%08Xu, vb_func_%08X);' % (addr, addr))
        out.append('')
    out += ['    return 0;', '}']
    return nl.join(out) + nl


# --------------------------------------------------------------------------
# Toolchain
# --------------------------------------------------------------------------

def find_vcvars():
    for root in (os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                 os.environ.get("ProgramFiles", r"C:\Program Files")):
        for ed in ("Community", "Professional", "Enterprise", "BuildTools"):
            p = os.path.join(root, "Microsoft Visual Studio", "2022", ed,
                             "VC", "Auxiliary", "Build", "vcvars64.bat")
            if os.path.exists(p):
                return p
    return None


def find_recomp():
    for cfg in ("Release", "Debug"):
        p = os.path.join(ROOT, "build", cfg, "v810recomp.exe")
        if os.path.exists(p):
            return p
    return None


# The runtime, minus the SDL platform layer's dependencies. Compiled from
# source rather than linked against build/vbrecomp.lib so the suite needs
# nothing but a compiler -- and so it always tests the runtime in the working
# tree, not whatever was last built.
RUNTIME_SRC = ["vbrecomp.c", "cpu.c", "mem.c", "vip.c", "vsu.c", "timer.c",
               "gamepad.c", "interrupt.c", "platform_sdl2.c"]


def cl(vcvars, workdir, args):
    cmd = '"%s" >nul 2>&1 && cl /nologo %s' % (vcvars, args)
    return subprocess.run(cmd, cwd=workdir, shell=True, capture_output=True,
                          text=True)


# --------------------------------------------------------------------------

def parse_disasm(gen_c):
    """Pull `/* <addr>: <disassembly> */` out of the generated C.

    The decoder's own rendering of what we encoded, which is how a typo in the
    case table shows up as a wrong instruction rather than a wrong answer.
    """
    out = {}
    for m in re.finditer(r"/\* ([0-9A-F]{8}): (.+?) \*/", gen_c):
        out.setdefault(int(m.group(1), 16), []).append(m.group(2).strip())
    return out


def body_disasm(disasm, addr):
    """The case's own instructions, up to and including its trailing return."""
    lines = []
    for off in range(0, SLOT, 2):
        for line in disasm.get(addr + off, []):
            lines.append(line)
            if line.startswith("jmp"):
                return lines
    return lines


def check(case, regs, psw):
    """Return a list of human-readable mismatches for one case."""
    bad = []
    for key, want in case["expect"].items():
        if key == "psw":
            want_bits = sum(PSW_BITS[f] for f in want.split())
            got = psw & 0x0F
            if got != want_bits:
                bad.append("PSW flags: want %-12s got %s"
                           % (flags_str(want_bits) or "(none)",
                              flags_str(got) or "(none)"))
        elif regs[key] != want:
            bad.append("r%-2d: want %08X got %08X" % (key, want, regs[key]))
    return bad


def flags_str(bits):
    return " ".join(n for n in ("Z", "S", "OV", "CY") if bits & PSW_BITS[n])


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="python tools/conformance.py",
        description=__doc__.splitlines()[0])
    ap.add_argument("-k", metavar="SUBSTR", help="only cases whose name matches")
    ap.add_argument("--list", action="store_true",
                    help="show what each case decodes to, and run nothing")
    ap.add_argument("--keep", action="store_true",
                    help="keep the generated ROM, C and exe for inspection")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    cases = [c for c in CASES if not args.k or args.k in c["name"]]
    if not cases:
        print("no cases match %r" % args.k, file=sys.stderr)
        return 2

    recomp = find_recomp()
    if not recomp:
        print("ERROR: v810recomp not built (cmake --build build)", file=sys.stderr)
        return 2
    vcvars = find_vcvars()
    if not vcvars:
        print("ERROR: no MSVC found (looked for vcvars64.bat under VS 2022)",
              file=sys.stderr)
        return 2

    work = tempfile.mkdtemp(prefix="vbrecomp-conf-")
    if args.verbose or args.keep:
        print("workdir: %s" % work)
    try:
        rom, addrs = build_rom(cases)
        rom_path = os.path.join(work, "conformance.vb")
        with open(rom_path, "wb") as f:
            f.write(rom)

        # Every case body is an `entry` hint, so analysis confirms it as a
        # function without needing a call from the reset path.
        hints = os.path.join(work, "conformance.hints.txt")
        with open(hints, "w") as f:
            f.write("# one entry per conformance case\n")
            for name, addr in addrs.items():
                f.write("entry %08X   # %s\n" % (addr, name))

        r = subprocess.run([recomp, rom_path, work, "--hints", hints],
                           capture_output=True, text=True)
        gen_c = os.path.join(work, "recomp_funcs.c")
        if r.returncode != 0 or not os.path.exists(gen_c):
            print("v810recomp failed:\n" + r.stdout + r.stderr, file=sys.stderr)
            return 1
        with open(gen_c) as f:
            source = f.read()
        disasm = parse_disasm(source)

        if args.list:
            for case in cases:
                print("%-34s %s" % (case["name"], case["why"]))
                for line in body_disasm(disasm, addrs[case["name"]]):
                    print("      %s" % line)
            return 0

        missing = [c["name"] for c in cases
                   if ("void vb_func_%08X(void) {" % addrs[c["name"]]) not in source]
        if missing:
            print("Not emitted (analysis did not confirm these entries): %s"
                  % ", ".join(missing), file=sys.stderr)
            return 1

        # An instruction the emitter does not implement becomes a comment, so
        # it runs as a no-op and the comparison below cannot see it. Say so.
        stubbed = {}
        for case in cases:
            addr = addrs[case["name"]]
            body = source.split("void vb_func_%08X(void) {" % addr, 1)[1]
            body = body.split("\n}", 1)[0]
            todo = re.findall(r"/\* (TODO:[^*]+|UNKNOWN [^*]+)\*/", body)
            if todo:
                stubbed[case["name"]] = [t.strip() for t in todo]

        with open(os.path.join(work, "harness.c"), "w") as f:
            f.write(harness_source(cases, addrs))

        srcs = " ".join('"%s"' % os.path.join(ROOT, "src", s) for s in RUNTIME_SRC)
        r = cl(vcvars, work,
               '/W3 /DVBRECOMP_NO_SDL /I"%s" /I"%s" harness.c recomp_funcs.c %s '
               '/Feharness.exe' % (os.path.join(ROOT, "include"), work, srcs))
        if r.returncode != 0:
            print("harness build failed:\n" + r.stdout + r.stderr, file=sys.stderr)
            return 1

        run = subprocess.run([os.path.join(work, "harness.exe"), rom_path],
                             capture_output=True, text=True, timeout=120)
        if run.returncode != 0:
            print(run.stdout)
            print("harness terminated abnormally: exit %d (0x%08X). The case "
                  "that faulted is the one after the last line above."
                  % (run.returncode, run.returncode & 0xFFFFFFFF), file=sys.stderr)
            if run.stderr.strip():
                print(run.stderr.strip(), file=sys.stderr)
            return 1

        results, faults = {}, {}
        for line in run.stdout.splitlines():
            parts = line.split()
            if len(parts) == 36 and parts[0] == "CASE":
                results[int(parts[1], 16)] = ([int(x, 16) for x in parts[2:34]],
                                              int(parts[35], 16))
            elif len(parts) == 3 and parts[0] == "FAULT":
                faults[int(parts[1], 16)] = int(parts[2], 16)

        failed = 0
        for case in cases:
            addr = addrs[case["name"]]
            if addr in faults:
                failed += 1
                print("FAIL %-34s %s" % (case["name"], case["why"]))
                for line in body_disasm(disasm, addr)[:-1]:
                    print("       %s" % line)
                print("       faulted on the host: %s"
                      % EXCEPTIONS.get(faults[addr], "0x%08X" % faults[addr]))
                continue
            if addr not in results:
                print("FAIL %-34s (no output -- did it return?)" % case["name"])
                failed += 1
                continue
            regs, psw = results[addr]
            bad = check(case, regs, psw)
            if bad:
                failed += 1
                print("FAIL %-34s %s" % (case["name"], case["why"]))
                for line in body_disasm(disasm, addr)[:-1]:
                    print("       %s" % line)
                for b in bad:
                    print("       %s" % b)
            elif args.verbose:
                print("ok   %-34s %s" % (case["name"], case["why"]))

        print("\n%d/%d cases pass" % (len(cases) - failed, len(cases)))
        if stubbed:
            print("\nInstructions the emitter left as a comment (they run as "
                  "no-ops, so a passing case above may be passing by accident):")
            for name, todos in sorted(stubbed.items()):
                print("  %-34s %s" % (name, "; ".join(todos)))
        return 1 if failed else 0
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
