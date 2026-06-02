# Ghidra pre-script: seed the Virtual Boy reset vector so auto-analysis has a
# starting point for function discovery. The reset stub sits at the top of the
# loaded image (0x07FFFFF0, the 0xFFFFFFF0 mirror) and tail-jumps to the real
# entry; Ghidra's analysis follows the jump and the JAL call graph from there.
#
# @category vbrecomp
# @runtime Jython

af = currentProgram.getAddressFactory()
sp = af.getDefaultAddressSpace()

def seed(off, name):
    a = sp.getAddress(off & 0xFFFFFFFF)
    if not currentProgram.getMemory().contains(a):
        return
    if getInstructionAt(a) is None:
        disassemble(a)
    if getFunctionAt(a) is None:
        try:
            createFunction(a, name)
        except:
            pass
    print("[seed_entry] seeded %s @ %08X" % (name, off))

# Reset vector (always the top 16 bytes of ROM, mapped to 0x07FFFFF0).
seed(0x07FFFFF0, "reset")
