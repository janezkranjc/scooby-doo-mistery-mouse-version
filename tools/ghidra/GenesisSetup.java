// Pre-analysis setup for a raw Sega Genesis / Mega Drive ROM image loaded at 0.
// Creates hardware memory blocks, labels I/O registers, types the vector table
// and seeds disassembly from every exception / interrupt vector.
//@category Genesis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;

public class GenesisSetup extends GhidraScript {
    private void block(String name, long start, long len, boolean exec) throws Exception {
        Memory mem = currentProgram.getMemory();
        Address a = toAddr(start);
        if (mem.getBlock(a) != null) return;
        MemoryBlock b = mem.createUninitializedBlock(name, a, len, false);
        b.setRead(true); b.setWrite(true); b.setExecute(exec); b.setVolatile(!name.equals("RAM"));
    }
    private void label(long addr, String name) throws Exception {
        createLabel(toAddr(addr), name, true, SourceType.USER_DEFINED);
    }
    @Override
    public void run() throws Exception {
        MemoryBlock rom = currentProgram.getMemory().getBlock(toAddr(0));
        rom.setName("ROM"); rom.setRead(true); rom.setWrite(false); rom.setExecute(true);
        block("RAM",     0xFF0000L, 0x10000, true);
        block("Z80RAM",  0xA00000L, 0x10000, false);
        block("IO",      0xA10000L, 0x100,   false);
        block("Z80CTRL", 0xA11000L, 0x300,   false);
        block("TMSS",    0xA14000L, 0x10,    false);
        block("VDP",     0xC00000L, 0x20,    false);

        label(0xC00000L, "VDP_DATA");   label(0xC00004L, "VDP_CTRL");
        label(0xC00008L, "VDP_HVCNT");  label(0xC00011L, "PSG");
        label(0xA10001L, "IO_VERSION"); label(0xA10003L, "IO_DATA1");
        label(0xA10005L, "IO_DATA2");   label(0xA10007L, "IO_DATA3");
        label(0xA10009L, "IO_CTRL1");   label(0xA1000BL, "IO_CTRL2");
        label(0xA1000DL, "IO_CTRL3");
        label(0xA11100L, "Z80_BUSREQ"); label(0xA11200L, "Z80_RESET");
        label(0xA14000L, "TMSS_REG");
        label(0xA04000L, "YM2612_A0");  label(0xA04001L, "YM2612_D0");
        label(0xA04002L, "YM2612_A1");  label(0xA04003L, "YM2612_D1");

        String[] vn = {"InitialSSP","Reset","BusError","AddressError","IllegalInstr","DivZero",
            "CHK","TRAPV","Privilege","Trace","LineA","LineF"};
        DataType ptr = new Pointer32DataType();
        for (int i = 0; i < 64; i++) {
            Address va = toAddr(i * 4L);
            try { clearListing(va, va.add(3)); createData(va, ptr); } catch (Exception e) {}
            long tgt = getInt(va) & 0xFFFFFFFFL;
            String nm = i < vn.length ? vn[i] : (i == 26 ? "ExtInt" : i == 28 ? "HBlank" : i == 30 ? "VBlank"
                    : (i >= 32 && i < 48) ? "Trap" + (i - 32) : null);
            if (i == 0 || tgt == 0 || tgt >= 0x200000L || (tgt & 1) != 0) continue;
            Address ta = toAddr(tgt);
            disassemble(ta);
            if (getFunctionAt(ta) == null) createFunction(ta, nm == null ? null : "vec_" + nm);
            addEntryPoint(ta);
        }
        // cartridge header strings
        int[][] hs = {{0x100,16},{0x110,16},{0x120,48},{0x150,48},{0x180,14},{0x190,16},{0x1F0,16}};
        String[] hn = {"hdr_console","hdr_copyright","hdr_title_domestic","hdr_title_overseas","hdr_serial","hdr_devices","hdr_region"};
        for (int i = 0; i < hs.length; i++) {
            Address a = toAddr(hs[i][0]);
            try { clearListing(a, a.add(hs[i][1]-1)); createData(a, new ArrayDataType(CharDataType.dataType, hs[i][1], 1)); label(hs[i][0], hn[i]); } catch (Exception e) {}
        }
        println("GenesisSetup done");
    }
}
