package java.awt;

public class BorderLayout implements LayoutManager {
    public static final String CENTER = "Center";
    private Component center;

    public void addLayoutComponent(String name, Component component) {
        if (name != null && !CENTER.equals(name)) {
            throw new IllegalArgumentException("Only BorderLayout.CENTER is supported");
        }
        center = component;
    }

    public void removeLayoutComponent(Component component) {
        if (component == center) {
            center = null;
        }
    }

    public Dimension preferredLayoutSize(Container parent) {
        return center == null ? new Dimension() : center.getPreferredSize();
    }

    public Dimension minimumLayoutSize(Container parent) {
        return preferredLayoutSize(parent);
    }

    public void layoutContainer(Container parent) {
        if (center != null) {
            center.width = parent.getWidth();
            center.height = parent.getHeight();
        }
    }
}
