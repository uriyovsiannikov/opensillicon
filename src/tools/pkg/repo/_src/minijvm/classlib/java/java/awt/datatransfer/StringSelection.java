package java.awt.datatransfer;
public class StringSelection implements Transferable, ClipboardOwner {
    private final String text;
    public StringSelection(String text) { this.text = text; }
    public DataFlavor[] getTransferDataFlavors() { return new DataFlavor[] {DataFlavor.stringFlavor}; }
    public boolean isDataFlavorSupported(DataFlavor flavor) { return DataFlavor.stringFlavor.equals(flavor); }
    public Object getTransferData(DataFlavor flavor) throws UnsupportedFlavorException {
        if (!isDataFlavorSupported(flavor)) throw new UnsupportedFlavorException(flavor);
        return text;
    }
    public void lostOwnership(Clipboard clipboard, Transferable contents) {}
}
