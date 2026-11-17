package java.awt;

public class Container extends Component {
    private LayoutManager layout = new BorderLayout();
    Component child;

    public void setLayout(LayoutManager manager) {
        if (manager == null) {
            throw new UnsupportedOperationException("Absolute layout is unavailable");
        }
        if (child != null) {
            manager.addLayoutComponent(BorderLayout.CENTER, child);
        }
        layout = manager;
    }

    public void add(Component component, Object constraints) {
        if (child != null || component.isDisplayable()) {
            throw new UnsupportedOperationException("Only one child per container is supported");
        }
        layout.addLayoutComponent((String) constraints, component);
        child = component;
    }

    public void removeAll() {
        if (child != null) {
            if (child.peer != 0) {
                X11Peer.destroy(child.peer);
                child.peer = 0;
            }
            layout.removeLayoutComponent(child);
            child = null;
        }
    }

    @Override
    public Dimension getPreferredSize() {
        return child == null ? super.getPreferredSize() : layout.preferredLayoutSize(this);
    }

    public void validate() {
        layout.layoutContainer(this);
        if (child != null && peer != 0) {
            if (child.peer == 0) {
                child.peer = X11Peer.createCanvas(peer, child.width, child.height);
            } else {
                X11Peer.resize(child.peer, child.width, child.height);
            }
        }
    }
}
