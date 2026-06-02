# Ghidra post-script: export the functions Ghidra discovered to a TSV file,
# for cross-validation against v810recomp's function table.
#
# Usage (via analyzeHeadless -postScript export_functions.py <out_path>):
#   each line: <hexAddr>\t<sizeBytes>\t<name>
#
# @category vbrecomp
# @runtime Jython

args = getScriptArgs()
out_path = args[0] if args else "ghidra_funcs.txt"

fm = currentProgram.getFunctionManager()
lines = []
for f in fm.getFunctions(True):  # True = iterate forward (address order)
    ep = f.getEntryPoint()
    size = f.getBody().getNumAddresses()
    lines.append("%08X\t%d\t%s" % (ep.getOffset(), size, f.getName()))

fh = open(out_path, "w")
fh.write("\n".join(lines))
fh.write("\n")
fh.close()
print("[export_functions] wrote %d functions to %s" % (len(lines), out_path))
