# Ghidra pre-script: seed the Virtual Boy reset vector + real entry point and
# enable aggressive code discovery, so Ghidra finds functions independently of
# v810recomp (for cross-validation).
#
# The reset stub at 0x07FFFFF0 is MOVHI hi,r0,r1 / MOVEA lo,r1,r1 / JMP [r1].
# We read those immediates straight from memory (robust, no operand model
# dependency), compute the entry, and seed both as functions. Aggressive
# Instruction Finder + decompiler switch analysis then expand the call graph.
#
# @category vbrecomp
# @runtime Jython

af = currentProgram.getAddressFactory()
sp = af.getDefaultAddressSpace()
mem = currentProgram.getMemory()

def A(off):
    return sp.getAddress(off & 0xFFFFFFFF)

def u16(off):
    b0 = mem.getByte(A(off)) & 0xFF
    b1 = mem.getByte(A(off + 1)) & 0xFF
    return b0 | (b1 << 8)

def seed(off, name):
    a = A(off)
    if not mem.contains(a):
        return
    if getInstructionAt(a) is None:
        try: disassemble(a)
        except: return
    if getFunctionAt(a) is None:
        try: createFunction(a, name)
        except: pass
    print("[seed_entry] seeded %s @ %08X" % (name, off))

# Enable aggressive discovery for raw-binary analysis.
for opt in ["Aggressive Instruction Finder", "Decompiler Switch Analysis"]:
    try:
        setAnalysisOption(currentProgram, opt, "true")
    except:
        pass

RESET = 0x07FFFFF0
seed(RESET, "reset")

# Derive the entry from the reset stub's MOVHI(hi) + MOVEA(lo) immediates.
try:
    imm_hi = u16(RESET + 2)        # MOVHI immediate (halfword after opcode hw)
    imm_lo = u16(RESET + 6)        # MOVEA immediate
    lo_s = (imm_lo ^ 0x8000) - 0x8000      # sign-extend 16-bit
    entry = ((imm_hi << 16) + lo_s) & 0xFFFFFFFF
    if entry >= 0x08000000:
        entry &= 0x07FFFFFF        # collapse high mirror to 27-bit ROM space
    seed(entry, "entry")
    print("[seed_entry] entry = %08X" % entry)
except Exception as e:
    print("[seed_entry] entry derivation failed: %s" % e)
