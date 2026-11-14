#ifndef SCINSTALL_UI_H
#define SCINSTALL_UI_H

#include <stdbool.h>
#include <stddef.h>

struct disk_choice {
	char path[32];
	char model[41];
	char serial[256];
	unsigned long long mib;
	bool writable;
};

bool ui_open(void);
bool ui_active(void);
void ui_close(void);
int ui_choose_disk(const struct disk_choice *, size_t count);
void ui_reset(void);
void ui_note(const char *);
void ui_progress(size_t done, size_t total);
bool ui_confirm(const char *phrase);
bool ui_reboot(void);
void ui_wait(void);

#endif
