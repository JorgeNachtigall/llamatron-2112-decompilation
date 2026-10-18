// Disassemble and create functions from an address list (used to close code holes).
// Script args: <targets.txt>
// Lines: "seg:off"                     code entry point
//        "table seg:off count [cs]"   word table of near offsets; targets are cs:<word>
//                                      (cs defaults to the table's own segment)
//        "# ..."                       comment
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class Disasm extends GhidraScript {
	@Override
	public void run() throws Exception {
		List<Address> targets = new ArrayList<>();
		AddressFactory af = currentProgram.getAddressFactory();
		Memory mem = currentProgram.getMemory();
		for (String line : java.nio.file.Files.readAllLines(new File(getScriptArgs()[0]).toPath())) {
			line = line.trim();
			if (line.isEmpty() || line.startsWith("#")) continue;
			String[] p = line.split("\\s+");
			if (p[0].equals("table")) {
				Address t = af.getAddress(p[1]);
				int n = Integer.decode(p[2]);
				String cs = p.length > 3 ? p[3] : p[1].split(":")[0];
				for (int i = 0; i < n; i++) {
					int w = mem.getShort(t.add(2L * i)) & 0xffff;
					targets.add(af.getAddress(cs + ":" + Integer.toHexString(w)));
				}
			}
			else targets.add(af.getAddress(p[0]));
		}
		int made = 0;
		for (Address a : targets) {
			if (getInstructionAt(a) == null) {
				if (getDataAt(a) != null && getDataAt(a).isDefined()) clearListing(a, a.add(1));
				disassemble(a);
				made++;
			}
			if (getFunctionAt(a) == null && getFunctionContaining(a) == null) createFunction(a, null);
		}
		println("targets=" + targets.size() + " newly disassembled=" + made);
	}
}
