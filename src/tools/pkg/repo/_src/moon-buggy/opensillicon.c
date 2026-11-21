#include <time.h>
#include <signal.h>
#include <unistd.h>

double
difftime(time_t end, time_t beginning)
{
	return (double)(end - beginning);
}

int
raise(int signo)
{
	return kill(getpid(), signo);
}
