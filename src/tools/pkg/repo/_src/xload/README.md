# xload

X.Org xload 1.1.4, adapted to draw OpenSillicon's runnable process count
using the process snapshot ABI. This is a point-in-time runnable-task count,
not the traditional Unix exponentially averaged load. Remote-host and keyboard
LED readings return zero because those services are not provided by libc.
