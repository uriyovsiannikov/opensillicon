package java.awt;

import java.awt.event.WindowEvent;
import java.awt.event.WindowListener;
import java.util.ArrayList;

public class Window extends Container {
    private String title;
    private final ArrayList<WindowListener> listeners = new ArrayList<WindowListener>();
    private volatile boolean visible;
    private boolean centered;
    private Thread eventThread;

    Window(String title) {
        this.title = title;
    }

    synchronized String getWindowTitle() {
        return title;
    }

    synchronized void setWindowTitle(String title) {
        this.title = title;
        if (peer != 0) {
            X11Peer.setTitle(peer, title);
        }
    }

    public synchronized void pack() {
        Dimension size = getPreferredSize();
        width = Math.max(1, size.width);
        height = Math.max(1, size.height);
        if (peer != 0) {
            X11Peer.resize(peer, width, height);
        }
        validate();
    }

    public synchronized void setLocationRelativeTo(Component component) {
        if (component != null) {
            throw new UnsupportedOperationException("Only screen centering is supported");
        }
        centered = true;
        if (peer != 0) {
            X11Peer.center(peer, width, height);
        }
    }

    public synchronized void setVisible(boolean show) {
        if (show == visible) {
            return;
        }
        if (show && peer == 0) {
            pack();
            peer = X11Peer.createWindow(title, width, height);
            if (peer == 0) {
                throw new AWTError("Cannot open X11 display. Start TinyX with startx.");
            }
            if (centered) {
                X11Peer.center(peer, width, height);
            }
            validate();
        }
        X11Peer.setMapped(peer, show);
        visible = show;
        if (show && eventThread == null) {
            eventThread = new Thread(new Runnable() {
                public void run() {
                    dispatchEvents();
                }
            }, "AWT event dispatch");
            eventThread.start();
        }
    }

    public boolean isVisible() {
        return visible;
    }

    public synchronized void dispose() {
        if (peer != 0) {
            X11Peer.destroy(peer);
            peer = 0;
            if (child != null) {
                child.peer = 0;
            }
        }
        visible = false;
    }

    public synchronized void addWindowListener(WindowListener listener) {
        if (listener != null) {
            listeners.add(listener);
        }
    }

    private void dispatchEvents() {
        for (;;) {
            WindowListener[] closing = null;
            synchronized (this) {
                if (!visible) {
                    eventThread = null;
                    return;
                }
                long event = X11Peer.poll(peer);
                if (event == -1) {
                    closing = listeners.toArray(new WindowListener[listeners.size()]);
                } else if (event != 0) {
                    width = (int) (event >>> 32);
                    height = (int) event;
                    validate();
                }
            }
            if (closing != null) {
                WindowEvent event = new WindowEvent(this, WindowEvent.WINDOW_CLOSING);
                for (WindowListener listener : closing) {
                    listener.windowClosing(event);
                }
            }
            try {
                Thread.sleep(20);
            } catch (InterruptedException interrupted) {
                // Visibility and disposal determine the lifetime of the event loop.
            }
        }
    }
}
