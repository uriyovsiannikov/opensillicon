public class SchedulerProbe implements Runnable {
    private final int id;
    private static volatile boolean first, second;
    private static volatile long left, right;
    SchedulerProbe(int id) { this.id = id; }
    public void run() {
        if (id == 0) first = true; else second = true;
        while (!first || !second) {}
        for (int i = 0; i < 1000000; i++) {
            if (id == 0) left += i; else right += i;
        }
    }
    public static void main(String[] args) throws Exception {
        long start = System.currentTimeMillis();
        Thread a = new Thread(new SchedulerProbe(0));
        Thread b = new Thread(new SchedulerProbe(1));
        a.start(); b.start(); a.join(); b.join();
        if (left != 499999500000L || right != left) throw new AssertionError("Concurrent loops");
        System.out.println("JAVA SCHEDULER PASS " + (System.currentTimeMillis() - start) + " ms");
    }
}
