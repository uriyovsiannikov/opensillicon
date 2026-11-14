#include "include/os.h"

static int
find_in_bin(char *path, size_t capacity, size_t prefix, const char *name)
{
	static const char bin[] = "/bin/";
	struct u_stat st;
	size_t length = u_strlen(name);

	if (prefix + sizeof(bin) > capacity ||
	    length >= capacity - prefix - (sizeof(bin) - 1))
		return 0;
	for (size_t i = 0; i < sizeof(bin) - 1; i++)
		path[prefix++] = bin[i];
	for (size_t i = 0; i <= length; i++)
		path[prefix + i] = name[i];
	return u_stat_path(path, &st) == 0 && st.type == 1 && (st.mode & 0111U);
}

int
u_find_command(const char *name, char *path, size_t capacity)
{
	static const char user_prefix[] = "/tmp/pkg-";
	struct os_credentials credentials;
	struct u_stat st;
	char digits[10];
	size_t count = 0;

	if (*name == '\0')
		return 0;
	if (find_in_bin(path, capacity, 0, name))
		return 1;
	if (u_getcredentials(&credentials) < 0 || credentials.effective_uid == 0)
		return 0;
	uint32_t uid = credentials.effective_uid;
	do {
		digits[count++] = '0' + uid % 10;
		uid /= 10;
	} while (uid != 0);
	size_t prefix = sizeof(user_prefix) - 1;
	if (prefix + count >= capacity)
		return 0;
	for (size_t i = 0; i < prefix; i++)
		path[i] = user_prefix[i];
	while (count != 0)
		path[prefix++] = digits[--count];
	path[prefix] = '\0';
	if (u_stat_path(path, &st) < 0 || st.type != 2 ||
	    st.uid != credentials.effective_uid || (st.mode & 0077U))
		return 0;
	return find_in_bin(path, capacity, prefix, name);
}

size_t
u_strlen(const char *string)
{
	size_t length = 0;
	while (string[length] != '\0') length++;
	return length;
}

int
u_streq(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static int
append_component(char *output, size_t capacity, size_t *length,
	const char *component, size_t component_length)
{
	size_t i;
	if (component_length == 0 || (component_length == 1 && component[0] == '.'))
		return 1;
	if (component_length == 2 && component[0] == '.' && component[1] == '.') {
		if (*length > 1) {
			while (*length > 1 && output[*length - 1] != '/') (*length)--;
			if (*length > 1) (*length)--;
			output[*length] = '\0';
		}
		return 1;
	}
	if (*length + (*length > 1) + component_length >= capacity)
		return 0;
	if (*length > 1) output[(*length)++] = '/';
	for (i = 0; i < component_length; i++) output[(*length)++] = component[i];
	output[*length] = '\0';
	return 1;
}

int
u_normalize_path(const char *cwd, const char *path, char *output,
	size_t capacity)
{
	const char *source;
	size_t length = 1;
	if (path == 0 || output == 0 || capacity < 2) return 0;
	output[0] = '/';
	output[1] = '\0';
	if (path[0] != '/') {
		if (cwd == 0 || cwd[0] != '/') return 0;
		source = cwd + 1;
		while (*source != '\0') {
			const char *begin = source;
			while (*source != '\0' && *source != '/') source++;
			if (!append_component(output, capacity, &length, begin,
			    (size_t)(source - begin))) return 0;
			while (*source == '/') source++;
		}
	}
	source = path + (path[0] == '/' ? 1 : 0);
	while (*source != '\0') {
		const char *begin = source;
		while (*source != '\0' && *source != '/') source++;
		if (!append_component(output, capacity, &length, begin,
		    (size_t)(source - begin))) return 0;
		while (*source == '/') source++;
	}
	return 1;
}
