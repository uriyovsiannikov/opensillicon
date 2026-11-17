package java.awt;

import java.awt.image.ImageObserver;

public abstract class Image {
    public abstract int getWidth(ImageObserver observer);
    public abstract int getHeight(ImageObserver observer);
    public abstract Graphics getGraphics();
}
