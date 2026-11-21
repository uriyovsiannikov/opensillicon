package java.awt.datatransfer;
public class DataFlavor {
    public static final DataFlavor stringFlavor = new DataFlavor(String.class, "Unicode String");
    private final Class representation;
    private final String name;
    public DataFlavor(Class representation, String name) {
        if (representation == null) throw new NullPointerException();
        this.representation = representation; this.name = name;
    }
    public Class getRepresentationClass() { return representation; }
    public String getHumanPresentableName() { return name; }
    public boolean equals(Object other) {
        return other instanceof DataFlavor && representation == ((DataFlavor)other).representation;
    }
    public int hashCode() { return representation.hashCode(); }
}
