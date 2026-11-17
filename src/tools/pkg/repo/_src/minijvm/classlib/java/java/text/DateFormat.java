package java.text;

import java.util.Date;

public abstract class DateFormat extends Format {
    public final String format(Date date) {
        return format(date, new StringBuffer(), new FieldPosition(0)).toString();
    }

    public StringBuffer format(Object value, StringBuffer target, FieldPosition position) {
        if (value instanceof Number) value = new Date(((Number) value).longValue());
        if (!(value instanceof Date)) throw new IllegalArgumentException("Expected a date");
        return format((Date) value, target, position);
    }

    public abstract StringBuffer format(Date date, StringBuffer target, FieldPosition position);
    public abstract Date parse(String text, ParsePosition position);

    public Date parse(String text) throws ParseException {
        ParsePosition position = new ParsePosition(0);
        Date date = parse(text, position);
        if (position.getIndex() == 0) throw new ParseException("Invalid date: " + text, position.getErrorIndex());
        return date;
    }
}
