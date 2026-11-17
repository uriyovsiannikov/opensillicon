#include <stdlib.h>

/* libos currently parses floating-point input at double precision. */
long double
strtold(const char *text, char **end)
{
	return strtod(text, end);
}
