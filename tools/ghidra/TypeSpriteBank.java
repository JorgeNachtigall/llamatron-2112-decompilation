// Type the game86 sprite bank (segment 218C, see docs/re-notes-game86.md section 5): pointer
// tables, one SpriteHeader per sprite and its pixel rows. Headless only on
// ghidra/headless/LlamatronCLI.gpr (see tools/ghidra/README.md); idempotent.
//
// Script args: [seg] [dry]      seg defaults to 218c
//
// Layout (offsets in the segment):
//   0000  word[14]   pointers read as sprite indices -14..-1 (index i reads 001C + 2*i)
//   001C  word[473]  sprite pointer table, indices 0..472; entries 414..417 point at four
//                    8-entry frame groups at 0318..0357 (inside this table), entry 418 = FFFF
//   03CE  sprites:   SpriteHeader (12 bytes) + (height_m1 + 1) rows of 10 * (cols_m1 + 1) bytes
//                    (per 16-px cell: 2 mask bytes, then 4 planes x 2 bytes)
// Labels: spr_<index> at each sprite (lowest index that points to it; spr_m<n> for index -n).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class TypeSpriteBank extends GhidraScript {
	static final int FIXED = 14, TABLE = 473, FIRST = 0x3ce, HDR = 12;

	@Override
	public void run() throws Exception {
		String seg = "218c";
		boolean dry = false;
		for (String a : getScriptArgs()) {
			if (a.equals("dry")) dry = true;
			else seg = a;
		}
		Memory mem = currentProgram.getMemory();
		AddressFactory af = currentProgram.getAddressFactory();
		Address base = af.getAddress(seg + ":0000");
		StructureDataType hdr = new StructureDataType("SpriteHeader", 0);
		hdr.add(WordDataType.dataType, "magic", "always 3");
		hdr.add(WordDataType.dataType, "cols_m1", "16-px cells - 1; 0 -> sprite_draw_core, else sprite_draw_wide");
		hdr.add(WordDataType.dataType, "height_m1", "rows - 1; centring uses height_m1/2");
		hdr.add(WordDataType.dataType, "width", "pixel width used for centring (x -= width/2)");
		hdr.add(WordDataType.dataType, "zero8", "always 0");
		hdr.add(WordDataType.dataType, "zeroA", "always 0");
		DataType hdrT = currentProgram.getDataTypeManager().addDataType(hdr, DataTypeConflictHandler.REPLACE_HANDLER);

		// index -> pointer
		TreeMap<Integer, String> names = new TreeMap<>();  // sprite offset -> label
		for (int i = -FIXED; i < TABLE; i++) {
			int p = mem.getShort(base.add(0x1c + 2L * i)) & 0xffff;
			if (p < FIRST || p == 0xffff) continue;
			names.putIfAbsent(p, i < 0 ? "spr_m" + (-i) : "spr_" + i);
		}
		int nT = 0, nL = 0, nD = 0;
		if (!dry) {
			nT += define(base, new ArrayDataType(WordDataType.dataType, FIXED, 2), "spr_fixed_ptrs");
			nT += define(base.add(0x1c), new ArrayDataType(WordDataType.dataType, TABLE, 2), "spr_ptr_table");
			label(base.add(0x318), "spr_frame_groups", "4 groups x 8 sprite pointers (table entries 414..417 point here)");
		}
		for (Map.Entry<Integer, String> e : names.entrySet()) {
			Address a = base.add(e.getKey());
			int cols = (mem.getShort(a.add(2)) & 0xffff) + 1;
			int rows = (mem.getShort(a.add(4)) & 0xffff) + 1;
			if ((mem.getShort(a) & 0xffff) != 3 || cols > 4 || rows > 200) {
				printerr(a + " " + e.getValue() + ": not a sprite header, skipped");
				continue;
			}
			nD++;
			if (dry) continue;
			nT += define(a, hdrT, e.getValue());
			nT += define(a.add(HDR), new ArrayDataType(ByteDataType.dataType, rows * 10 * cols, 1), null);
			nL++;
		}
		println(String.format("%s%s %s: sprites=%d labels=%d types_changed=%d", dry ? "[dry] " : "",
			currentProgram.getName(), seg, nD, nL, nT));
	}

	private int define(Address a, DataType dt, String name) throws Exception {
		if (name != null) label(a, name, null);
		Data cur = getDataAt(a);
		if (cur != null && cur.isDefined() && cur.getDataType().isEquivalent(dt)) return 0;
		clearListing(a, a.add(dt.getLength() - 1));
		createData(a, dt);
		return 1;
	}

	private void label(Address a, String name, String comment) throws Exception {
		SymbolTable st = currentProgram.getSymbolTable();
		Symbol prim = st.getPrimarySymbol(a);
		if (prim == null || !prim.getName().equals(name)) {
			Symbol s = st.createLabel(a, name, SourceType.USER_DEFINED);
			s.setPrimary();
		}
		if (comment != null) currentProgram.getListing().setComment(a, CommentType.PRE, comment);
	}
}
