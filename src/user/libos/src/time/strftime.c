#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *const weekdays[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const months[] = {
	"January", "February", "March", "April", "May", "June", "July", "August",
	"September", "October", "November", "December"
};

static int
leap(long year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

/* The runtime's localtime is UTC and its only locale is C. */
size_t
strftime(char *destination, size_t capacity, const char *format, const struct tm *value)
{
	size_t used = 0;
	if (capacity == 0 || destination == NULL || format == NULL || value == NULL)
		return 0;
	while (*format != '\0') {
		char buffer[128];
		const char *text = buffer;
		size_t length = 1;
		long number = 0;
		int width = 2, numeric = 0;
		char pad = '0';
		long year = (long)value->tm_year + 1900;
		buffer[0] = *format++;
		if (buffer[0] == '%') {
			if (*format == 'E' || *format == 'O')
				format++;
			char conversion = *format;
			if (conversion == '\0')
				return 0;
			format++;
			const char *nested = NULL;
			switch (conversion) {
			case '%': buffer[0] = '%'; break;
			case 'n': buffer[0] = '\n'; break;
			case 't': buffer[0] = '\t'; break;
			case 'a': case 'A':
				text = value->tm_wday >= 0 && value->tm_wday < 7 ?
				    weekdays[value->tm_wday] : "?";
				length = conversion == 'a' && text[0] != '?' ? 3 : strlen(text);
				break;
			case 'b': case 'B': case 'h':
				text = value->tm_mon >= 0 && value->tm_mon < 12 ?
				    months[value->tm_mon] : "?";
				length = conversion != 'B' && text[0] != '?' ? 3 : strlen(text);
				break;
			case 'p': text = value->tm_hour < 12 ? "AM" : "PM"; length = 2; break;
			case 'P': text = value->tm_hour < 12 ? "am" : "pm"; length = 2; break;
			case 'z': text = "+0000"; length = 5; break;
			case 'Z': text = "UTC"; length = 3; break;
			case 'c': nested = "%a %b %e %H:%M:%S %Y"; break;
			case 'x': case 'D': nested = "%m/%d/%y"; break;
			case 'X': case 'T': nested = "%H:%M:%S"; break;
			case 'F': nested = "%Y-%m-%d"; break;
			case 'r': nested = "%I:%M:%S %p"; break;
			case 'R': nested = "%H:%M"; break;
			case 'C': number = year / 100; numeric = 1; break;
			case 'd': case 'e':
				number = value->tm_mday; numeric = 1;
				if (conversion == 'e') pad = ' ';
				break;
			case 'H': case 'k':
				number = value->tm_hour; numeric = 1;
				if (conversion == 'k') pad = ' ';
				break;
			case 'I': case 'l':
				number = value->tm_hour % 12;
				if (number == 0) number = 12;
				numeric = 1;
				if (conversion == 'l') pad = ' ';
				break;
			case 'j': number = value->tm_yday + 1; width = 3; numeric = 1; break;
			case 'm': number = value->tm_mon + 1; numeric = 1; break;
			case 'M': number = value->tm_min; numeric = 1; break;
			case 'S': number = value->tm_sec; numeric = 1; break;
			case 'u': number = value->tm_wday == 0 ? 7 : value->tm_wday; width = 1; numeric = 1; break;
			case 'w': number = value->tm_wday; width = 1; numeric = 1; break;
			case 'U': number = (value->tm_yday + 7 - value->tm_wday) / 7; numeric = 1; break;
			case 'W': number = (value->tm_yday + 7 - (value->tm_wday + 6) % 7) / 7; numeric = 1; break;
			case 'y': number = year % 100; if (number < 0) number += 100; numeric = 1; break;
			case 'Y': number = year; width = 4; numeric = 1; break;
			case 's': {
				struct tm copy = *value;
				number = (long)mktime(&copy); width = 1; numeric = 1;
				break;
			}
			case 'g': case 'G': case 'V': {
				int day = value->tm_wday == 0 ? 7 : value->tm_wday;
				int week = (value->tm_yday + 11 - day) / 7;
				int january = (value->tm_wday - value->tm_yday % 7 + 7) % 7;
				if (week == 0) {
					year--;
					january = (january + 7 - 1 - leap(year)) % 7;
					week = january == 4 || (january == 3 && leap(year)) ? 53 : 52;
				} else if (week == 53 && january != 4 && !(january == 3 && leap(year))) {
					year++; week = 1;
				}
				number = conversion == 'V' ? week : conversion == 'g' ? year % 100 : year;
				width = conversion == 'G' ? 4 : 2; numeric = 1;
				break;
			}
			default: buffer[0] = '%'; buffer[1] = conversion; length = 2; break;
			}
			if (nested != NULL) {
				length = strftime(buffer, sizeof(buffer), nested, value);
				if (length == 0) return 0;
			} else if (numeric) {
				int result = snprintf(buffer, sizeof(buffer), pad == '0' ? "%0*ld" : "%*ld", width, number);
				if (result < 0 || (size_t)result >= sizeof(buffer)) return 0;
				length = (size_t)result;
			}
		}
		if (length >= capacity - used) {
			destination[used] = '\0';
			return 0;
		}
		memcpy(destination + used, text, length);
		used += length;
	}
	destination[used] = '\0';
	return used;
}

char *
ctime(const time_t *value)
{
	static char buffer[26];
	struct tm *date = localtime(value);
	if (date == NULL || strftime(buffer, sizeof(buffer), "%a %b %e %T %Y\n", date) == 0) {
		errno = EOVERFLOW;
		return NULL;
	}
	return buffer;
}
