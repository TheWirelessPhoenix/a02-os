// Create Thumb functions at push-prologue sites in banked blocks (every initialized executable block),
// then decompile every function to <out_dir>/<program>.c.
// Usage (headless): -postScript BankFuncs.java <out_dir>
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

import java.io.File;
import java.io.PrintWriter;

public class BankFuncs extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			if (!b.isInitialized() || !b.isExecute()) continue;
			long n = b.getSize();
			Address s = b.getStart();
			for (long o = 0; o + 4 <= n; o += 2) {
				Address a = s.add(o);
				int h = getShort(a) & 0xffff;
				boolean push = (h & 0xff00) == 0xb500 || h == 0xe92d;
				if ((o == 0 || push) && getFunctionContaining(a) == null && getInstructionAt(a) == null) {
					new ArmDisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
					new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
				}
			}
		}
		File out = new File(outDir, currentProgram.getName().replace(".elf", "") + ".c");
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		try (PrintWriter w = new PrintWriter(out)) {
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				DecompileResults r = di.decompileFunction(f, 60, monitor);
				w.println("// ---- " + f.getName() + " @ " + f.getEntryPoint());
				w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
			}
		}
		di.dispose();
		println("wrote " + out);
	}
}
