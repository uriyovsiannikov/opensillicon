package java.security;

public class MessageDigest {
    private final String algorithm;
    private final int digestLength;
    private final byte[] state;

    private MessageDigest(String algorithm, int type, int digestLength) {
        this.algorithm = algorithm;
        this.digestLength = digestLength;
        state = createState(type);
    }

    public static MessageDigest getInstance(String algorithm) throws NoSuchAlgorithmException {
        if (algorithm == null) throw new NullPointerException("algorithm");
        if ("MD5".equalsIgnoreCase(algorithm)) return new MessageDigest("MD5", 0, 16);
        if ("SHA-1".equalsIgnoreCase(algorithm) || "SHA1".equalsIgnoreCase(algorithm))
            return new MessageDigest("SHA-1", 1, 20);
        if ("SHA-256".equalsIgnoreCase(algorithm) || "SHA256".equalsIgnoreCase(algorithm))
            return new MessageDigest("SHA-256", 2, 32);
        throw new NoSuchAlgorithmException(algorithm);
    }

    public String getAlgorithm() { return algorithm; }
    public final int getDigestLength() { return digestLength; }

    public void update(byte value) {
        update(new byte[]{value});
    }

    public void update(byte[] input) {
        update(input, 0, input.length);
    }

    public void update(byte[] input, int offset, int length) {
        if (offset < 0 || length < 0 || offset > input.length - length)
            throw new IllegalArgumentException("Invalid digest input region");
        updateState(state, input, offset, length);
    }

    public byte[] digest() {
        return finishState(state);
    }

    public byte[] digest(byte[] input) {
        update(input);
        return digest();
    }

    public void reset() {
        resetState(state);
    }

    private static native byte[] createState(int type);
    private static native void updateState(byte[] state, byte[] input, int offset, int length);
    private static native byte[] finishState(byte[] state);
    private static native void resetState(byte[] state);
}
