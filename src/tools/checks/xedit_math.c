#include <float.h>
#include <math.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "mp.h"

int
main(void)
{
	mpi left, right, result;
	mpi_init(&left); mpi_init(&right); mpi_init(&result);
	mpi_seti(&left, -1234567);
	mpi_seti(&right, 7654321);
	mpi_add(&result, &left, &right);
	if (mpi_cmpi(&result, 6419754) != 0) return 1;
	mpi_mul(&result, &left, &right);
	if (mpi_getd(&result) != -9449772114007.0) return 2;
	const double values[] = {0.0, -0.0, 12345.75, -12345.75, INFINITY, -INFINITY, NAN};
	for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
		mpi_setd(&result, values[i]);
		double actual = mpi_getd(&result);
		double expected = isnan(values[i]) ? 0.0 :
		    isinf(values[i]) ? copysign(DBL_MAX, values[i]) : trunc(values[i]);
		if (actual != expected) return 3;
	}
	mpi_clear(&result); mpi_clear(&right); mpi_clear(&left);
	/* The editor's removed creat wrapper had exactly this libc contract. */
	int fd = creat("/tmp/xedit-creat-check", 0600);
	if (fd < 0 || write(fd, "data", 4) != 4 || close(fd) != 0) return 4;
	fd = creat("/tmp/xedit-creat-check", 0600);
	if (fd < 0 || lseek(fd, 0, SEEK_END) != 0 || close(fd) != 0 ||
	    unlink("/tmp/xedit-creat-check") != 0) return 5;
	puts("XEDIT_MATH_PASS");
	return 0;
}
