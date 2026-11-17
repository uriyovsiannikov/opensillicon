package java.awt.event;

public abstract class WindowAdapter implements WindowListener {
    public void windowOpened(WindowEvent event) {}
    public void windowClosing(WindowEvent event) {}
    public void windowClosed(WindowEvent event) {}
    public void windowIconified(WindowEvent event) {}
    public void windowDeiconified(WindowEvent event) {}
    public void windowActivated(WindowEvent event) {}
    public void windowDeactivated(WindowEvent event) {}
}
