// Apply re/symbols.csv (names, comments, optional data types) to the current program.
// Headless only on ghidra/headless/LlamatronCLI.gpr (see tools/ghidra/README.md); the user may run
// the same script in the GUI project, so it must stay idempotent.
//
// CSV columns (header line required, RFC-4180 quoting for commas/quotes in the comment):
//   program,address,name,type,comment[,dtype]
//   program  game86 | fs | llama | install   (matches "<program>" or "<program>.exe")
//   address  seg:off in hex, e.g. 1000:1d40 or 3559:39e4
//   type     func  -> create/rename function, comment becomes the plate comment
//            data  -> primary label, comment becomes an EOL comment, dtype applied when given
//            label -> primary label (code or data), comment becomes a pre comment
//   dtype    optional, data rows only: byte word dword ptr16 farptr char[N] byte[N] word[N] dword[N]
//
// Script args: <csv> [dry] [types]
//   dry    report what would change, modify nothing
//   types  apply dtype columns (clears undefined bytes only, never instructions or defined data)
//
// Function names replace Ghidra's FUN_<seg>_<off>; the old default name is kept in the plate comment
// as "aka FUN_..." so cross references to older listings still resolve.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ApplySymbols extends GhidraScript {
	private boolean dry;
	private boolean types;
	private int nFunc, nLabel, nComment, nType, nSkip;

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 1) {
			printerr("usage: ApplySymbols.java <symbols.csv> [dry] [types]");
			return;
		}
		for (int i = 1; i < args.length; i++) {
			if (args[i].equals("dry")) dry = true;
			else if (args[i].equals("types")) types = true;
		}
		String prog = currentProgram.getName().toLowerCase();
		List<String> lines = Files.readAllLines(Paths.get(args[0]));
		Set<String> seenAddr = new HashSet<>();
		for (int ln = 1; ln < lines.size(); ln++) {
			String line = lines.get(ln);
			if (line.isBlank() || line.startsWith("#")) continue;
			List<String> f = parseCsv(line);
			if (f.size() < 4) {
				printerr("line " + (ln + 1) + ": too few columns");
				nSkip++;
				continue;
			}
			String p = f.get(0).trim().toLowerCase();
			if (!prog.equals(p) && !prog.equals(p + ".exe")) continue;
			String addrStr = f.get(1).trim(), name = f.get(2).trim(), type = f.get(3).trim();
			String comment = f.size() > 4 ? f.get(4).trim() : "";
			String dtype = f.size() > 5 ? f.get(5).trim() : "";
			if (!seenAddr.add(type + "@" + addrStr)) {
				printerr("line " + (ln + 1) + ": duplicate " + type + " row for " + addrStr);
			}
			Address a = currentProgram.getAddressFactory().getAddress(addrStr);
			if (a == null || currentProgram.getMemory().getBlock(a) == null) {
				printerr("line " + (ln + 1) + ": address not in memory: " + addrStr);
				nSkip++;
				continue;
			}
			try {
				switch (type) {
					case "func" -> applyFunc(a, name, comment);
					case "data" -> applyLabel(a, name, comment, dtype, true);
					case "label" -> applyLabel(a, name, comment, "", false);
					default -> {
						printerr("line " + (ln + 1) + ": unknown type " + type);
						nSkip++;
					}
				}
			}
			catch (Exception e) {
				printerr("line " + (ln + 1) + " " + addrStr + " " + name + ": " + e.getMessage());
				nSkip++;
			}
		}
		println(String.format("%s%s: functions=%d labels=%d comments=%d types=%d skipped=%d",
			dry ? "[dry] " : "", currentProgram.getName(), nFunc, nLabel, nComment, nType, nSkip));
	}

	private void applyFunc(Address a, String name, String comment) throws Exception {
		Function fn = getFunctionAt(a);
		String aka = null;
		if (fn == null) {
			if (getInstructionAt(a) == null) {
				Data d = getDataAt(a);
				if (d != null && d.isDefined()) {
					printerr(a + " " + name + ": defined data at function address, not disassembling");
					nSkip++;
					return;
				}
				if (!dry && !disassemble(a)) {
					printerr(a + " " + name + ": disassembly failed");
					nSkip++;
					return;
				}
			}
			Function outer = getFunctionContaining(a);
			if (outer != null) {
				// Shared tail inside another body (e.g. a table entry pointing into a handler):
				// a label keeps the name visible without splitting the outer function.
				applyLabel(a, name, comment, "", false);
				println(a + " " + name + ": inside " + outer.getName() + ", applied as label");
				return;
			}
			if (!dry) fn = createFunction(a, name);
			if (!dry && fn == null) {
				printerr(a + " " + name + ": createFunction failed");
				nSkip++;
				return;
			}
		}
		else if (!fn.getName().equals(name)) {
			if (fn.getSymbol().getSource() == SourceType.DEFAULT) aka = fn.getName();
			if (!dry) {
				// A plain label of the same name (e.g. from an earlier label row) would block the rename.
				for (Symbol s : currentProgram.getSymbolTable().getSymbols(a)) {
					if (s.getSymbolType() == SymbolType.LABEL && s.getName().equals(name)) s.delete();
				}
				fn.setName(name, SourceType.USER_DEFINED);
			}
		}
		else {
			setCommentIfChanged(a, CommentType.PLATE, comment, null);
			return;
		}
		nFunc++;
		setCommentIfChanged(a, CommentType.PLATE, comment, aka);
	}

	private void applyLabel(Address a, String name, String comment, String dtype, boolean isData)
			throws Exception {
		SymbolTable st = currentProgram.getSymbolTable();
		Symbol prim = st.getPrimarySymbol(a);
		if (prim == null || !prim.getName().equals(name)) {
			nLabel++;
			if (dry) println("label " + a + " " + name + " (now " + (prim == null ? "-" : prim.getName()) + ")");
			if (!dry) {
				if (prim != null && prim.getSource() == SourceType.USER_DEFINED &&
					prim.getSymbolType() == SymbolType.LABEL) {
					prim.setName(name, SourceType.USER_DEFINED);
				}
				else {
					Symbol s = st.createLabel(a, name, SourceType.USER_DEFINED);
					if (!s.isPrimary() && prim.getSymbolType() != SymbolType.FUNCTION) s.setPrimary();
				}
			}
		}
		if (isData && types && !dtype.isEmpty()) applyType(a, name, dtype);
		setCommentIfChanged(a, isData ? CommentType.EOL : CommentType.PRE, comment, null);
	}

	private void applyType(Address a, String name, String dtype) throws Exception {
		DataType dt = parseType(dtype);
		if (dt == null) {
			printerr(a + " " + name + ": unknown dtype " + dtype);
			return;
		}
		Data cur = getDataAt(a);
		if (cur != null && cur.isDefined() && cur.getDataType().isEquivalent(dt)) return;
		Address end = a.add(dt.getLength() - 1);
		Listing l = currentProgram.getListing();
		for (Address x = a; x.compareTo(end) <= 0; x = x.next()) {
			CodeUnit cu = l.getCodeUnitContaining(x);
			if (cu instanceof Instruction) {
				printerr(a + " " + name + ": instruction inside " + dtype + " range, type not applied");
				return;
			}
		}
		nType++;
		if (dry) {
			println("type " + a + " " + name + " " + dtype + " (now " + (cur == null ? "-" : cur.getDataType().getName()) + ")");
			return;
		}
		clearListing(a, end);
		createData(a, dt);
	}

	private DataType parseType(String s) {
		int n = 1;
		String base = s;
		int br = s.indexOf('[');
		if (br > 0) {
			n = Integer.decode(s.substring(br + 1, s.indexOf(']')));
			base = s.substring(0, br);
		}
		DataType dt = switch (base) {
			case "byte" -> ByteDataType.dataType;
			case "char" -> CharDataType.dataType;
			case "word", "ptr16" -> WordDataType.dataType;
			case "dword", "farptr" -> DWordDataType.dataType;
			default -> null;
		};
		if (dt == null || br < 0) return dt;
		return new ArrayDataType(dt, n, dt.getLength());
	}

	private void setCommentIfChanged(Address a, CommentType kind, String comment, String aka) {
		String text = comment;
		if (aka != null) text = text.isEmpty() ? "aka " + aka : text + "\naka " + aka;
		if (text.isEmpty()) return;
		String old = currentProgram.getListing().getComment(kind, a);
		if (aka == null && old != null && old.contains("\naka FUN_")) {
			text = text + old.substring(old.indexOf("\naka FUN_"));
		}
		if (text.equals(old)) return;
		nComment++;
		if (!dry) currentProgram.getListing().setComment(a, kind, text);
	}

	static List<String> parseCsv(String line) {
		List<String> out = new ArrayList<>();
		StringBuilder cur = new StringBuilder();
		boolean q = false;
		for (int i = 0; i < line.length(); i++) {
			char c = line.charAt(i);
			if (q) {
				if (c == '"' && i + 1 < line.length() && line.charAt(i + 1) == '"') {
					cur.append('"');
					i++;
				}
				else if (c == '"') q = false;
				else cur.append(c);
			}
			else if (c == '"') q = true;
			else if (c == ',') {
				out.add(cur.toString());
				cur.setLength(0);
			}
			else cur.append(c);
		}
		out.add(cur.toString());
		return out;
	}
}
