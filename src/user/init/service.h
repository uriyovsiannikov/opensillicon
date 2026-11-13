#ifndef OPENSILLICON_SERVICE_H
#define OPENSILLICON_SERVICE_H

#define SERVICE_SOCKET "/var/run/svctl.sock"
#define SERVICE_NAME_SIZE 32

enum service_operation {
	SERVICE_LIST = 1,
	SERVICE_STATUS,
	SERVICE_START,
	SERVICE_STOP,
	SERVICE_RESTART
};

struct service_request {
	unsigned char operation;
	char name[SERVICE_NAME_SIZE];
};

_Noreturn void service_manager(void);

#endif
