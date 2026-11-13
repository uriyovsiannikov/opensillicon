# Services

PID 1 reads `/etc/service.d/*.conf` once at boot. Each filename gives the
service name, for example `console.conf` defines `console`. Names are up to
31 ASCII letters, digits, underscores or hyphens. The directory and the files
must belong to root and cannot be writable by group or others.

```ini
command=/bin/sh
uid=1000
gid=1000
priority=100
restart=always
enabled=yes
console=yes
```

`command`, `uid` and `gid` are required. The command starts with an absolute
executable path, followed by optional arguments separated by spaces or tabs.
PID 1 executes it directly. Shell quoting, expansion and redirection are
unavailable in this field. Blank lines and lines starting with `#` are ignored.

- `priority` defaults to 100. Lower numbers are launched first. Names break
  ties. Priority orders launches and does not indicate service readiness.
- `restart` defaults to `never`. `always` also restarts after a clean exit.
  `on-failure` restarts after a nonzero exit status. Automatic restarts wait one
  second between attempts.
- `enabled` defaults to `yes`. Set it to `no` for manual startup.
- `console` defaults to `no`, which uses `/dev/null` for standard input.
  The bundled console service uses the system terminal. Both modes inherit
  standard output and standard error from init.

```sh
svctl list
svctl status console
svctl start myservice
svctl stop myservice
svctl restart myservice
```

Any user can query status. The last three commands require effective UID 0.
The default console runs as UID 1000. An accepted command schedules an action.
Use `svctl status` to see the resulting process state and last exit code.
Stopping sends SIGTERM to the service process, then SIGKILL after three
seconds if it is still running. A manual stop suppresses automatic restart.
Services should stay in the foreground because init supervises the process
it launched. Live config reload is unavailable.

To build a separate disk image with the console config and `/bin/svctl`:

```sh
make root ROOT_IMAGE=src/build/services.img
make iso
make run-gui ROOT_IMAGE=src/build/services.img
```

To include your own services, place their configs in `src/user/init/`, or
set `SERVICE_CONFIGS` to a list of files when building a new image. Include
`src/user/init/console.conf` in that list to keep the default console.
