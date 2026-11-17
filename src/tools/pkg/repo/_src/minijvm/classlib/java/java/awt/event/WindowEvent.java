package java.awt.event;

import java.awt.Window;
import java.util.EventObject;

public class WindowEvent extends EventObject {
    public static final int WINDOW_CLOSING = 201;
    private final int id;

    public WindowEvent(Window window, int id) {
        super(window);
        this.id = id;
    }

    public Window getWindow() {
        return (Window) getSource();
    }

    public int getID() {
        return id;
    }
}
