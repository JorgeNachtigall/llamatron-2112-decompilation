// Write <outdir>/<program>.functions.csv: entry,name,body_bytes,instructions,callers,callees
// Used by gen_funcmap.py to seed docs/funcmap.csv (addresses and names only).
// Script args: <outdir>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class ExportFunctions extends GhidraScript {
	@Override
	public void run() throws Exception {
		String out = getScriptArgs()[0] + "/" + currentProgram.getName() + ".functions.csv";
		Listing l = currentProgram.getListing();
		try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
			w.println("entry,name,body_bytes,instructions,callers,callees");
			for (Function f : l.getFunctions(true)) {
				int n = 0;
				for (Instruction i : l.getInstructions(f.getBody(), true)) n++;
				w.printf("%s,%s,%d,%d,%d,%d%n", f.getEntryPoint(), f.getName(),
					f.getBody().getNumAddresses(), n, f.getCallingFunctions(monitor).size(),
					f.getCalledFunctions(monitor).size());
			}
		}
		println("wrote " + out);
	}
}
