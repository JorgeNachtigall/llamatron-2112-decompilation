// Write <outdir>/<program>.lst: a plain-text listing of every initialized block with function
// headers, labels, symbolic operands, comments and up to ~60 chars of xrefs per line.
// The output contains original code and data: it goes to re/listings/ only (never committed).
// Script args: <outdir>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;

public class Export extends GhidraScript {
	@Override
	public void run() throws Exception {
		String out = getScriptArgs()[0] + "/" + currentProgram.getName() + ".lst";
		CodeUnitFormat fmt = new CodeUnitFormat(CodeUnitFormatOptions.ShowBlockName.NEVER,
			CodeUnitFormatOptions.ShowNamespace.NEVER);
		try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
			Listing l = currentProgram.getListing();
			for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
				if (!b.isInitialized()) continue;
				w.println("; ===== block " + b.getName() + " " + b.getStart() + " - " + b.getEnd());
				CodeUnitIterator it = l.getCodeUnits(new AddressSet(b.getStart(), b.getEnd()), true);
				long undefRun = 0;
				Address undefStart = null;
				while (it.hasNext()) {
					CodeUnit cu = it.next();
					boolean undef = (cu instanceof Data) && !((Data) cu).isDefined();
					String plate = cu.getComment(CommentType.PLATE);
					String pre = cu.getComment(CommentType.PRE);
					String eol = cu.getComment(CommentType.EOL);
					boolean named = cu.getLabel() != null;
					if (undef && !named && plate == null && pre == null && eol == null) {
						if (undefRun++ == 0) undefStart = cu.getAddress();
						continue;
					}
					if (undefRun > 0) {
						w.println(undefStart + "  ; .. " + undefRun + " undefined bytes");
						undefRun = 0;
					}
					Function f = getFunctionAt(cu.getAddress());
					if (f != null) w.println("\n; ---- FUNCTION " + f.getName());
					if (plate != null) for (String s : plate.split("\n")) w.println("; | " + s);
					if (pre != null) for (String s : pre.split("\n")) w.println("; " + s);
					String lbl = cu.getLabel();
					if (lbl != null) w.println(lbl + ":");
					String refs = "";
					for (var r : getReferencesTo(cu.getAddress())) {
						if (refs.length() > 60) {
							refs += " ...";
							break;
						}
						refs += " " + r.getFromAddress() + "(" +
							r.getReferenceType().getName().charAt(0) + ")";
					}
					String text = fmt.getRepresentationString(cu);
					if (eol != null) text += "    ; " + eol.replace("\n", " / ");
					w.println(cu.getAddress() + "  " + text + (refs.isEmpty() ? "" : "    ;xref" + refs));
				}
				if (undefRun > 0) w.println(undefStart + "  ; .. " + undefRun + " undefined bytes");
			}
		}
		println("wrote " + out);
	}
}
