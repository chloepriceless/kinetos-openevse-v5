// Create functions at the given addresses (if missing) and print their decompiled C.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
            for (int i = 1; i < a.length; i++) {
                Address ad = toAddr(a[i]);
                Function f = getFunctionAt(ad);
                if (f == null) {
                    new DisassembleCommand(ad, null, true).applyTo(currentProgram, monitor);
                    f = createFunction(ad, null);
                }
                if (f == null) f = getFunctionContaining(ad);
                w.println("// ===== " + (f == null ? "NONE" : f.getName()) + " @ " + ad);
                if (f == null) continue;
                DecompileResults r = d.decompileFunction(f, 120, monitor);
                if (r != null && r.decompileCompleted()) w.println(r.getDecompiledFunction().getC());
            }
        }
    }
}
