package java.awt;
import java.awt.datatransfer.Clipboard;
public class Toolkit {
    private static final Toolkit instance = new Toolkit();
    private final Clipboard clipboard = new Clipboard("OpenSillicon");
    public static Toolkit getDefaultToolkit() { return instance; }
    public Clipboard getSystemClipboard() { return clipboard; }
}
