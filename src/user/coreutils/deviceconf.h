#ifndef OPENSILLICON_DEVICECONF_H
#define OPENSILLICON_DEVICECONF_H

int deviceconf_show(const char *name, const char *node, const char *empty);
int deviceconf_main(int argc, char **argv, const char *name, const char *node,
    const char *usage, const char *empty);

#endif
