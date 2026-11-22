#ifndef ABI_EXEC_H
#define ABI_EXEC_H

/* Native execve limits. Byte budgets include each terminating NUL. */
#define OS_EXEC_ARG_MAX 16U
#define OS_EXEC_ENV_MAX 64U
#define OS_EXEC_ARG_BYTES_MAX 2048U
#define OS_EXEC_ENV_BYTES_MAX 8192U

#endif
