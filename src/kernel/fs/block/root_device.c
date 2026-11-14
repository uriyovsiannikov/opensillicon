#include <string.h>
#include "common/uuid.h"
#include "kernel/root_device.h"
#include "kernel/errno.h"
int
root_device_select(const char *cmdline, struct block_device **selected)
{
	static const char prefix[] = "root=PARTUUID=";
	uint8_t uuid[16];
	bool found = false;
	if (!selected)
		return -EINVAL;
	*selected = NULL;
	const char *p = cmdline;
	while (p && *p) {
		while (*p == ' ' || *p == '\t')
			p++;
		const char *end = p;
		while (*end && *end != ' ' && *end != '\t')
			end++;
		size_t length = (size_t)(end - p);
		if (length >= 5 && memcmp(p, "root=", 5) == 0) {
			if (found || length != sizeof(prefix) - 1 + 36 ||
			    memcmp(p, prefix, sizeof(prefix) - 1) != 0 ||
			    !uuid_parse(p + sizeof(prefix) - 1, 36, uuid))
				return -EINVAL;
			found = true;
		}
		p = end;
	}
	if (!found)
		return 0;
	for (size_t i = 0; i < block_device_count(); i++) {
		struct block_device *d = block_device_at(i);
		if (d->parent && memcmp(d->partition_uuid, uuid, 16) == 0) {
			if (*selected) {
				*selected = NULL;
				return -EEXIST;
			}
			*selected = d;
		}
	}
	return *selected ? 1 : -ENOENT;
}
