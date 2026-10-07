// Disassemble and create functions at addresses listed in a seeds file.
// Each line: HEXADDR KIND NAME   (KIND F = function entry, D = disassemble only)
// Usage: -postScript SeedFromTrace.java <seeds.txt>
//@category Genesis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.util.*;

public class SeedFromTrace extends GhidraScript {
    @Override
    public void run() throws Exception {
        List<String[]> seeds = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
            String l;
            while ((l = r.readLine()) != null) { l = l.trim(); if (!l.isEmpty()) seeds.add(l.split("\\s+")); }
        }
        Listing listing = currentProgram.getListing();
        int dis = 0, fn = 0;
        for (String[] s : seeds) {
            Address a = toAddr(Long.parseLong(s[0], 16));
            if (listing.getInstructionAt(a) == null) {
                Data d = listing.getDefinedDataContaining(a);
                if (d != null) clearListing(d.getMinAddress(), d.getMaxAddress());
                if (disassemble(a)) dis++;
            }
        }
        analyzeChanges(currentProgram);
        for (String[] s : seeds) {
            Address a = toAddr(Long.parseLong(s[0], 16));
            if (listing.getInstructionAt(a) == null) continue;
            boolean want = s[1].equals("F");
            if (!want && getFunctionContaining(a) == null) {
                Instruction prev = listing.getInstructionBefore(a);
                want = prev == null || !prev.hasFallthrough() || !prev.getMaxAddress().add(1).equals(a);
            }
            if (!want) continue;
            Function f = getFunctionAt(a);
            if (f == null) {
                Function c = getFunctionContaining(a);
                if (c != null && !s[1].equals("F")) continue;
                f = createFunction(a, null);
                if (f != null) fn++;
            }
            if (f != null && s.length > 2 && !s[2].isEmpty() && f.getName().startsWith("FUN_")) f.setName(s[2], SourceType.USER_DEFINED);
        }
        analyzeChanges(currentProgram);
        println("SeedFromTrace: disassembled " + dis + " seeds, created " + fn + " functions");
    }
}
