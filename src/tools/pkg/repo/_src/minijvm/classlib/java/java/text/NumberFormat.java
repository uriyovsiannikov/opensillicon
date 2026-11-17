package java.text;

import java.util.Locale;

public abstract class NumberFormat extends Format {
    public static NumberFormat getIntegerInstance(Locale locale) {
        if (!Locale.US.getLanguage().equals(locale.getLanguage())) {
            throw new UnsupportedOperationException("Number formatting supports English locales");
        }
        return new DecimalFormat("#,##0");
    }

    public abstract String format(long value);
    public abstract String format(double value);

    public StringBuffer format(Object value, StringBuffer target, FieldPosition position) {
        if (!(value instanceof Number)) {
            throw new IllegalArgumentException("Expected a Number");
        }
        if (value instanceof Long || value instanceof Integer ||
                value instanceof Short || value instanceof Byte) {
            return target.append(format(((Number) value).longValue()));
        }
        return target.append(format(((Number) value).doubleValue()));
    }
}
