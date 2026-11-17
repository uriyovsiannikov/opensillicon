package java.awt.image;

import java.awt.Image;

public interface ImageObserver {
    boolean imageUpdate(Image image, int flags, int x, int y, int width, int height);
}
