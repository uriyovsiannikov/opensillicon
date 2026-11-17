package java.text;

import java.math.BigDecimal;
import java.math.RoundingMode;

public class DecimalFormat extends NumberFormat {
    private final int minimumIntegerDigits;
    private final int minimumFractionDigits;
    private final int maximumFractionDigits;
    private final int groupingSize;

    public DecimalFormat() {
        this("#,##0.###");
    }

    public DecimalFormat(String pattern) {
        int decimal = pattern.indexOf('.');
        String integer = decimal < 0 ? pattern : pattern.substring(0, decimal);
        String fraction = decimal < 0 ? "" : pattern.substring(decimal + 1);
        int minimumInteger = 0;
        int minimumFraction = 0;
        for (int i = 0; i < integer.length(); i++) {
            char digit = integer.charAt(i);
            if (digit == '0') minimumInteger++;
            else if (digit != '#' && digit != ',') {
                throw new IllegalArgumentException("Unsupported numeric pattern");
            }
        }
        boolean optional = false;
        for (int i = 0; i < fraction.length(); i++) {
            char digit = fraction.charAt(i);
            if (digit == '0' && !optional) minimumFraction++;
            else if (digit == '#') optional = true;
            else throw new IllegalArgumentException("Unsupported fractional pattern");
        }
        int comma = integer.lastIndexOf(',');
        groupingSize = comma < 0 ? 0 : integer.length() - comma - 1;
        if (integer.length() == 0 || (comma >= 0 && groupingSize == 0)) {
            throw new IllegalArgumentException("Empty integer pattern");
        }
        minimumIntegerDigits = minimumInteger;
        minimumFractionDigits = minimumFraction;
        maximumFractionDigits = fraction.length();
    }

    public String format(long value) {
        return formatDecimal(BigDecimal.valueOf(value));
    }

    public String format(double value) {
        if (Double.isNaN(value)) return "NaN";
        if (Double.isInfinite(value)) return value < 0 ? "-∞" : "∞";
        return formatDecimal(BigDecimal.valueOf(value));
    }

    @Override
    public StringBuffer format(Object value, StringBuffer target, FieldPosition position) {
        if (value instanceof BigDecimal) {
            return target.append(formatDecimal((BigDecimal) value));
        }
        if (value instanceof java.math.BigInteger) {
            return target.append(formatDecimal(new BigDecimal((java.math.BigInteger) value)));
        }
        return super.format(value, target, position);
    }

    private String formatDecimal(BigDecimal value) {
        String decimal = value.abs().setScale(maximumFractionDigits, RoundingMode.HALF_EVEN).toPlainString();
        int point = decimal.indexOf('.');
        String integer = point < 0 ? decimal : decimal.substring(0, point);
        while (integer.length() < minimumIntegerDigits) integer = "0" + integer;
        StringBuffer result = new StringBuffer();
        if (value.signum() < 0) result.append('-');
        for (int i = 0; i < integer.length(); i++) {
            if (i > 0 && groupingSize > 0 && (integer.length() - i) % groupingSize == 0) {
                result.append(',');
            }
            result.append(integer.charAt(i));
        }
        if (point >= 0) {
            String fraction = decimal.substring(point + 1);
            int end = fraction.length();
            while (end > minimumFractionDigits && fraction.charAt(end - 1) == '0') end--;
            if (end > 0) result.append('.').append(fraction.substring(0, end));
        }
        return result.toString();
    }
}
