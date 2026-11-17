package java.awt;

public class Color {
    public static final Color BLACK = new Color(0);
    public static final Color black = BLACK;
    public static final Color WHITE = new Color(0xffffff);
    public static final Color white = WHITE;
    private final int rgb;

    public Color(int rgb) {
        this.rgb = 0xff000000 | (rgb & 0xffffff);
    }

    public int getRGB() {
        return rgb;
    }
    public static Color getHSBColor(float hue, float saturation, float brightness) {
        return new Color(HSBtoRGB(hue, saturation, brightness));
    }

    public static int HSBtoRGB(float hue, float saturation, float brightness) {
        float red, green, blue;
        if (saturation == 0) {
            red = green = blue = brightness;
        } else {
            float position = (hue - (float) Math.floor(hue)) * 6;
            int sector = (int) position;
            float fraction = position - sector;
            float low = brightness * (1 - saturation);
            float falling = brightness * (1 - saturation * fraction);
            float rising = brightness * (1 - saturation * (1 - fraction));
            switch (sector) {
                case 0: red = brightness; green = rising; blue = low; break;
                case 1: red = falling; green = brightness; blue = low; break;
                case 2: red = low; green = brightness; blue = rising; break;
                case 3: red = low; green = falling; blue = brightness; break;
                case 4: red = rising; green = low; blue = brightness; break;
                default: red = brightness; green = low; blue = falling; break;
            }
        }
        return 0xff000000 | ((int) (red * 255 + 0.5f) << 16) |
                ((int) (green * 255 + 0.5f) << 8) | (int) (blue * 255 + 0.5f);
    }
}
