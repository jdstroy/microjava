public class GPIO
{
    public static final int IN = 1;
    public static final int IN_PD = 2;
    public static final int IN_PU = 3;
    public static final int OUT = 4;
    public static final int OD = 5;
    public static final int OD_PU = 6;

    public static int pin(int port, int pin) { return 0; }
    public static boolean init(int gpio_pin, int mode) { return true; }
    public static void clear(int gpio_pin) {}
    public static void set(int gpio_pin) {}
    public static void toggle(int gpio_pin) {}
    public static boolean read(int gpio_pin) { return true; }
    public static void write(int gpio_pin, boolean value) {}
}