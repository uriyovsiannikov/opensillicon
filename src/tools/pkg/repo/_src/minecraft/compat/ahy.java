import java.awt.Canvas;
import java.awt.Component;
import java.awt.Frame;
import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintStream;
import net.minecraft.client.Minecraft;
import net.minecraft.client.MinecraftApplet;

/** Minecraft 1.0.0 desktop adapter using console crash reporting on miniJVM. */
public class ahy extends Minecraft {
    final Frame a;

    public ahy(Component component, Canvas canvas, MinecraftApplet applet,
               int width, int height, boolean fullscreen, Frame frame) {
        super(component, canvas, applet, width, height, fullscreen);
        a = frame;
    }

    public void a(oq report) {
        System.err.println("Minecraft: " + report.a);
        if (report.b != null) report.b.printStackTrace();
        try {
            File directory = new File(System.getProperty("user.home", "/tmp"), ".minecraft");
            if (!directory.isDirectory()) directory.mkdirs();
            File log = new File(directory, "crash.log");
            PrintStream output = new PrintStream(new FileOutputStream(log));
            try {
                output.println("Minecraft 1.0.0: " + report.a);
                Throwable cause = report.b;
                for (int depth = 0; cause != null && depth < 16; depth++) {
                    output.println((depth == 0 ? "" : "Caused by: ") + cause);
                    StackTraceElement[] stack = cause.getStackTrace();
                    if (stack != null) {
                        for (int i = 0; i < stack.length; i++)
                            output.println("    at " + stack[i]);
                    }
                    Throwable next = cause.getCause();
                    if (next == cause) break;
                    cause = next;
                }
            } finally {
                output.close();
            }
            System.err.println("Minecraft crash report: " + log.getAbsolutePath());
        } catch (Exception failure) {
            System.err.println("Minecraft: could not save crash.log: " + failure);
        } finally {
            if (a != null) a.dispose();
        }
    }
}
