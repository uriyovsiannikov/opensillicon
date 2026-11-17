package java.util.zip;

final class ZipStream {
    private long handle;
    final int[] progress = new int[2];

    ZipStream(boolean compress, int windowBits) {
        handle = create(compress, windowBits);
        if (handle == 0) throw new OutOfMemoryError("Cannot allocate a compression stream");
    }

    synchronized int process(byte[] input, int offset, int length,
            byte[] output, int outputOffset, int outputLength, int flush) {
        if (handle == 0) throw new IllegalStateException("Compression stream is closed");
        return process(handle, input, offset, length, output, outputOffset, outputLength, flush, progress);
    }

    synchronized void reset() {
        if (handle == 0) throw new IllegalStateException("Compression stream is closed");
        reset(handle);
    }

    synchronized void end() {
        if (handle != 0) {
            release(handle);
            handle = 0;
        }
    }

    protected void finalize() {
        end();
    }

    private static native long create(boolean compress, int windowBits);
    private static native int process(long handle, byte[] input, int offset, int length,
            byte[] output, int outputOffset, int outputLength, int flush, int[] progress);
    private static native void reset(long handle);
    private static native void release(long handle);
}
