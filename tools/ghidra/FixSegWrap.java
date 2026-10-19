// Repair near calls/jumps whose 16-bit offset wraps past 64 KB, and clear the code that the
// first auto-analysis wrongly disassembled at the unwrapped (linear) targets.
// Headless only on ghidra/headless/LlamatronCLI.gpr (see tools/ghidra/README.md); idempotent.
//
// Script args: <code_seg> <data_seg> [dry]
//   code_seg  segment whose near branches wrap (game86: 1e7b)
//   data_seg  segment that received the bogus targets (game86: 218c, the embedded sprite bank)
//   dry       report what would change, modify nothing
//
// Ghidra adds a near branch's 16-bit target to the segment base without wrapping, so a branch
// at 1e7b:1ad0 to offset 0x0cd2 (rel16 negative) lands at linear 0x2f482 = 218c:dbc2. The
// script moves each such reference to code_seg:(offset & 0xffff), disassembles the real target
// if needed, then removes every function, instruction and analysis-made data unit and label
// in the data segment. User-defined labels in data_seg (from re/symbols.csv) are kept.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class FixSegWrap extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 2) {
			printerr("usage: FixSegWrap.java <code_seg> <data_seg> [dry]");
			return;
		}
		boolean dry = args.length > 2 && args[2].equals("dry");
		AddressFactory af = currentProgram.getAddressFactory();
		Memory mem = currentProgram.getMemory();
		Listing listing = currentProgram.getListing();
		ReferenceManager rm = currentProgram.getReferenceManager();
		Address codeBase = af.getAddress(args[0] + ":0000");
		Address dataBase = af.getAddress(args[1] + ":0000");
		MemoryBlock dataBlock = mem.getBlock(dataBase);
		AddressSet dataSet = new AddressSet(dataBlock.getStart(), dataBlock.getEnd());
		AddressSet codeSet = new AddressSet(mem.getBlock(codeBase).getStart(), mem.getBlock(codeBase).getEnd());
		long codeLinear = ((SegmentedAddress) codeBase).getOffset();

		// 1. Re-point wrapped branch references from code_seg into data_seg.
		int moved = 0, disas = 0;
		for (Instruction ins : listing.getInstructions(codeSet, true)) {
			for (Reference r : ins.getReferencesFrom()) {
				if (!dataSet.contains(r.getToAddress())) continue;
				long off = (r.getToAddress().getOffset() - codeLinear) & 0xffff;
				Address to = af.getAddress(args[0] + ":" + Long.toHexString(off));
				println(ins.getAddress() + " " + ins.getMnemonicString() + ": " + r.getToAddress() + " -> " + to);
				moved++;
				if (dry) continue;
				if (getInstructionAt(to) == null) {
					disassemble(to);
					disas++;
				}
				rm.delete(r);
				Reference nr = rm.addMemoryReference(ins.getAddress(), to, r.getReferenceType(),
					SourceType.USER_DEFINED, r.getOperandIndex());
				rm.setPrimary(nr, true);
			}
		}

		// 2. Clear the bogus analysis in data_seg.
		int funcs = 0, insns = 0, data = 0, syms = 0;
		for (Function f : listing.getFunctions(dataSet, true)) {
			funcs++;
			if (!dry) currentProgram.getFunctionManager().removeFunction(f.getEntryPoint());
		}
		for (Instruction ins : listing.getInstructions(dataSet, true)) insns++;
		for (Data d : listing.getDefinedData(dataSet, true)) {
			Symbol s = d.getPrimarySymbol();
			if (s == null || s.getSource() != SourceType.USER_DEFINED) data++;
		}
		if (!dry) {
			// Instructions only: clearing defined data would also drop typed tables from symbols.csv.
			for (Instruction ins : listing.getInstructions(dataSet, true)) {
				clearListing(ins.getMinAddress(), ins.getMaxAddress());
			}
			for (Data d : listing.getDefinedData(dataSet, true)) {
				Symbol s = d.getPrimarySymbol();
				if (s == null || s.getSource() != SourceType.USER_DEFINED) {
					clearListing(d.getMinAddress(), d.getMaxAddress());
				}
			}
		}
		// Remaining references made by the bogus code (data_seg -> anywhere) and its labels.
		List<Reference> stale = new ArrayList<>();
		for (Address a : rm.getReferenceSourceIterator(dataSet, true)) {
			for (Reference r : rm.getReferencesFrom(a)) {
				if (r.getSource() != SourceType.USER_DEFINED) stale.add(r);
			}
		}
		if (!dry) for (Reference r : stale) rm.delete(r);
		SymbolTable st = currentProgram.getSymbolTable();
		List<Symbol> dead = new ArrayList<>();
		for (Symbol s : st.getSymbols(dataSet, SymbolType.LABEL, true)) {
			if (s.getSource() != SourceType.USER_DEFINED && !s.isExternal()) dead.add(s);
		}
		syms = dead.size();
		if (!dry) for (Symbol s : dead) {
			if (!s.isDeleted() && !s.hasReferences()) s.delete();
		}
		println(String.format("%s%s: refs moved=%d disassembled=%d; %s functions=%d instructions=%d data=%d " +
			"stale refs=%d labels=%d", dry ? "[dry] " : "", currentProgram.getName(), moved, disas, args[1],
			funcs, insns, data, stale.size(), syms));
	}
}
