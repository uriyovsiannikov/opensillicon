package java.awt.datatransfer;
/** Process-local clipboard; X11 selection exchange is not implemented. */
public class Clipboard {
    private final String name;
    private Transferable contents;
    private ClipboardOwner owner;
    public Clipboard(String name) { this.name = name; }
    public String getName() { return name; }
    public synchronized Transferable getContents(Object requestor) { return contents; }
    public void setContents(Transferable next, ClipboardOwner nextOwner) {
        ClipboardOwner oldOwner; Transferable old;
        synchronized (this) { oldOwner = owner; old = contents; owner = nextOwner; contents = next; }
        if (oldOwner != null && oldOwner != nextOwner) oldOwner.lostOwnership(this, old);
    }
}
