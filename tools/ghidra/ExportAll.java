// Post-analysis export: function table, full disassembly listing, and decompiled C.
// Usage: -postScript ExportAll.java <outdir>
//@category Genesis
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ExportAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        new File(out).mkdirs();
        Listing listing = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(out + "/functions.tsv"))) {
            w.println("entry\tname\tsize\tcallers\tcallees");
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                int callers = 0;
                for (Reference r : rm.getReferencesTo(f.getEntryPoint())) if (r.getReferenceType().isCall()) callers++;
                w.printf("%06X\t%s\t%d\t%d\t%d%n", f.getEntryPoint().getOffset(), f.getName(),
                        f.getBody().getNumAddresses(), callers, f.getCalledFunctions(monitor).size());
            }
        }
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out + "/disasm.txt"), 1 << 20))) {
            InstructionIterator ii = listing.getInstructions(true);
            while (ii.hasNext()) {
                Instruction ins = ii.next();
                Address a = ins.getAddress();
                Function f = fm.getFunctionAt(a);
                if (f != null) w.printf("%n; ======== %s ========%n", f.getName());
                Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
                if (s != null && f == null) w.printf("%s:%n", s.getName());
                StringBuilder refs = new StringBuilder();
                for (Reference r : ins.getReferencesFrom()) {
                    if (r.getToAddress().isMemoryAddress()) refs.append(String.format(" ->%06X", r.getToAddress().getOffset()));
                }
                w.printf("%06X  %-44s ;%s%n", a.getOffset(), ins.toString(), refs);
            }
        }
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out + "/decomp.c"), 1 << 20))) {
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                w.printf("%n// ======== %s @ %06X ========%n", f.getName(), f.getEntryPoint().getOffset());
                if (r != null && r.decompileCompleted()) w.println(r.getDecompiledFunction().getC());
                else w.println("// decompile failed: " + (r == null ? "null" : r.getErrorMessage()));
            }
        }
        di.dispose();
        println("ExportAll done -> " + out);
    }
}
