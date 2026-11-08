# SPDX-License-Identifier: GPL-2.0-or-later
# Ghidra headless post-script (Jython): label an unpacked Keen EXE image from oracle/docs/symbols/kN.sym.
# usage: analyzeHeadless PROJ p -import IMAGE.bin -processor "x86:LE:16:Real Mode" -loader BinaryLoader \
#          -loader-baseAddr 1000:0000 -postScript ghidra_import_syms.py oracle/docs/symbols/k4.sym 1000 -scriptPath oracle/docs/symbols
# IMAGE.bin = the unpacked image (EXE minus header, see oracle/docs/SYMBOLS.md); args: <sym file> <load segment hex>.
from ghidra.program.model.symbol import SourceType

args = getScriptArgs()
symfile = args[0]
loadseg = int(args[1], 16) if len(args) > 1 else 0x1000
fac = currentProgram.getAddressFactory()
n = bad = 0
for line in open(symfile):
    if line.startswith('#') or not line.strip():
        continue
    name, seg, off, lin, kind, sn = line.rstrip('\n').split('\t')
    a = fac.getAddress('%04X:%04X' % ((loadseg + int(seg, 16)) & 0xFFFF, int(off, 16)))
    if a is None or not currentProgram.getMemory().contains(a):
        bad += 1
        continue
    createLabel(a, name, True, SourceType.IMPORTED)
    if kind == 'code' and getFunctionAt(a) is None:
        disassemble(a)
        createFunction(a, name)
    n += 1
println('ghidra_import_syms: %d labels, %d outside image' % (n, bad))
