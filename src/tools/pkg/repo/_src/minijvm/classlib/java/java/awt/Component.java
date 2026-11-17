package java.awt;

public abstract class Component {
    private Dimension preferredSize;
    int width;
    int height;
    volatile long peer;

    public void setPreferredSize(Dimension size) {
        preferredSize = size == null ? null : new Dimension(size);
    }

    public Dimension getPreferredSize() {
        return preferredSize == null ? new Dimension(width, height) : new Dimension(preferredSize);
    }

    public int getWidth() {
        return width;
    }

    public int getHeight() {
        return height;
    }

    public boolean isDisplayable() {
        return peer != 0;
    }

    public Graphics getGraphics() {
        return peer == 0 ? null : new Graphics(this);
    }
}
