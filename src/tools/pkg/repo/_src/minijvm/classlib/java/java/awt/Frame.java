package java.awt;

public class Frame extends Window {
    public Frame() {
        this("");
    }

    public Frame(String title) {
        super(title == null ? "" : title);
    }

    public String getTitle() {
        return getWindowTitle();
    }

    public void setTitle(String title) {
        setWindowTitle(title == null ? "" : title);
    }
}
