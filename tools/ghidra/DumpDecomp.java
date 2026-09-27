// Dump decompiled C of every function in the current program to <out_dir>/<program>.c
// Usage (headless): -postScript DumpDecomp.java <out_dir>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;

public class DumpDecomp extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
		File out = new File(outDir, currentProgram.getName().replace(".elf", "") + ".c");
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		try (PrintWriter w = new PrintWriter(out)) {
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				DecompileResults r = di.decompileFunction(f, 60, monitor);
				w.println("// ---- " + f.getName() + " @ " + f.getEntryPoint());
				if (r.decompileCompleted()) {
					w.println(r.getDecompiledFunction().getC());
				} else {
					w.println("// decompile failed: " + r.getErrorMessage());
				}
			}
		}
		di.dispose();
		println("wrote " + out);
	}
}
