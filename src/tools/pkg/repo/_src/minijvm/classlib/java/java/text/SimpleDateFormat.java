/* Copyright (c) 2008-2015, Avian Contributors

   Permission to use, copy, modify, and/or distribute this software
   for any purpose with or without fee is hereby granted, provided
   that the above copyright notice and this permission notice appear
   in all copies.

   There is NO WARRANTY for this software.  See license.txt for
   details. */

package java.text;

import java.util.Calendar;
import java.util.Date;
import java.util.TimeZone;

public class SimpleDateFormat extends DateFormat {
  private String pattern;

  private TimeZone zone = TimeZone.getDefault();

  public SimpleDateFormat() {
    this("yyyy-MM-dd HH:mm");
  }

  public SimpleDateFormat(String pattern) {
    this.pattern = pattern;
    boolean quoted = false;
    for (int i = 0; i < pattern.length(); i++) {
      char letter = pattern.charAt(i);
      if (letter == '\'') {
        quoted = !quoted;
      } else if (!quoted && ((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z')) && "yMdHms".indexOf(letter) < 0) {
        throw new IllegalArgumentException("Unsupported date field: " + letter);
      }
    }
    if (quoted) throw new IllegalArgumentException("Unterminated date pattern quote");
  }

  public void setTimeZone(TimeZone zone) {
    if (zone == null) throw new NullPointerException("zone");
    this.zone = zone;
  }

  public StringBuffer format(Date date, StringBuffer buffer, FieldPosition position) {
    Calendar calendar = Calendar.getInstance(zone);
    calendar.setTime(date);
    boolean quoted = false;
    for (int i = 0; i < pattern.length();) {
      char letter = pattern.charAt(i);
      if (letter == '\'') {
        if (i + 1 < pattern.length() && pattern.charAt(i + 1) == '\'') {
          buffer.append('\'');
          i += 2;
        } else {
          quoted = !quoted;
          i++;
        }
        continue;
      }
      if (quoted || "yMdHms".indexOf(letter) < 0) {
        buffer.append(letter);
        i++;
        continue;
      }
      int end = i + 1;
      while (end < pattern.length() && pattern.charAt(end) == letter) end++;
      if (letter == 'M' && end - i > 2)
        throw new UnsupportedOperationException("Textual month names are unavailable");
      int value;
      switch (letter) {
        case 'y': value = calendar.get(Calendar.YEAR); if (end - i == 2) value %= 100; break;
        case 'M': value = calendar.get(Calendar.MONTH) + 1; break;
        case 'd': value = calendar.get(Calendar.DAY_OF_MONTH); break;
        case 'H': value = calendar.get(Calendar.HOUR_OF_DAY); break;
        case 'm': value = calendar.get(Calendar.MINUTE); break;
        default: value = calendar.get(Calendar.SECOND); break;
      }
      pad(buffer, value, end - i);
      i = end;
    }
    return buffer;
  }

  public Date parse(String text) {
    return parse(text, new ParsePosition(0));
  }

  public Date parse(String text, ParsePosition position) {
    if (!"yyyy-MM-dd'T'HH:mm:ss".equals(pattern))
      throw new UnsupportedOperationException("Parsing this date pattern is unavailable");
    int index = position.getIndex();
    try {
      Calendar calendar = Calendar.getInstance(zone);
      index = parseField(text, index, 4, calendar, Calendar.YEAR, 0);
      index = expectPrefix(text, index, "-");
      index = parseField(text, index, 2, calendar, Calendar.MONTH, -1);
      index = expectPrefix(text, index, "-");
      index = parseField(text, index, 2, calendar, Calendar.DAY_OF_MONTH, 0);
      index = expectPrefix(text, index, "T");
      index = parseField(text, index, 2, calendar, Calendar.HOUR_OF_DAY, 0);
      index = expectPrefix(text, index, ":");
      index = parseField(text, index, 2, calendar, Calendar.MINUTE, 0);
      index = expectPrefix(text, index, ":");
      index = parseField(text, index, 2, calendar, Calendar.SECOND, 0);
      position.setIndex(index);
      return calendar.getTime();
    } catch (ParseException e) {
      position.setErrorIndex(index);
      return null;
    }
  }

  private static void pad(StringBuffer buffer, int value, int digits) {
    int i = value == 0 ? 1 : value;
    while (i > 0) {
      i /= 10;
      --digits;
    }
    while (digits-- > 0) {
      buffer.append('0');
    }
    buffer.append(value);
  }

  private static int parseField(String text, int offset, int length, Calendar calendar, int field, int adjustment) throws ParseException {
    if (text.length() < offset + length) throw new ParseException("Short date: " + text, offset);
    try {
      int value = Integer.parseInt(text.substring(offset, offset + length), 10);
      calendar.set(field, value + adjustment);
    } catch (NumberFormatException e) {
      throw new ParseException("Not a number: " + text, offset);
    }
    return offset + length;
  }

  private static int expectPrefix(String text, int offset, String prefix) throws ParseException {
    if (text.length() <= offset) throw new ParseException("Short date: " + text, offset);
    if (! text.substring(offset).startsWith(prefix)) throw new ParseException("Parse error: " + text, offset);
    return offset + prefix.length();
  }
}
