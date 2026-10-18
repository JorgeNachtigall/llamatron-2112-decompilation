// Print a table of words (near pointers / values) from the current program.
// Script args: <seg:off> <count> [cs]
// With cs given each word is also shown as cs:<word> together with the symbol there, which is what
// Disasm.java's "table" lines need.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;

public class DumpTable extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		AddressFactory af = currentProgram.getAddressFactory();
		Address t = af.getAddress(args[0]);
		int n = Integer.decode(args[1]);
		String cs = args.length > 2 ? args[2] : null;
		for (int i = 0; i < n; i++) {
			Address at = t.add(2L * i);
			int w = currentProgram.getMemory().getShort(at) & 0xffff;
			String line = String.format("%s [%2d] %04x", at, i, w);
			if (cs != null) {
				Address dst = af.getAddress(cs + ":" + Integer.toHexString(w));
				Symbol s = dst == null ? null : getSymbolAt(dst);
				line += "  -> " + dst + (s != null ? " " + s.getName() : "") +
					(getInstructionAt(dst) == null ? "  (not code)" : "");
			}
			println(line);
		}
	}
}
