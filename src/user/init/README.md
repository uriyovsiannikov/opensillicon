# Services

PID 1 reads `/etc/service.d/*.conf` at boot and on reload. Each filename gives the
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
  second between attempts. After five consecutive automatic restart triggers
  from runs shorter than ten seconds, a non-console service becomes
  `rate-limited`. Use `svctl start` or `svctl restart` to clear the limit.
- `enabled` defaults to `yes`. Set it to `no` for manual startup.
- `console` defaults to `no`, which uses `/dev/null` for standard input.
  Non-console services write stdout and stderr to
  `/var/log/services/<name>.log`, readable by root. Output is appended on
  each run. The bundled console service keeps all three terminal streams.

```sh
svctl list
svctl status console
svctl start myservice
svctl stop myservice
svctl restart myservice
svctl reload
```

Any user can query status. Changing service state or reloading requires effective UID 0.
The default console runs as UID 1000. An accepted command schedules an action.
Use `svctl status` to see the resulting process state and last exit code.
Stopping sends SIGTERM to the service process, then SIGKILL after three
seconds if it is still running. A manual stop suppresses automatic restart.
Services should stay in the foreground because init supervises the process
it launched.

`svctl reload` validates the entire configuration before applying it. Running
processes keep their PIDs and streams. Changed launch settings apply at the
next start. Newly enabled services are queued in priority order. Stop a
running service before removing its configuration file. Disabling a service
on reload leaves its current process running and suppresses automatic restarts.

To build a separate disk image with the console config and `/bin/svctl`:

```sh
make root ROOT_IMAGE=src/build/services.img
make iso
make run-gui ROOT_IMAGE=src/build/services.img
```

To include your own services, place their configs in `src/user/init/`, or
set `SERVICE_CONFIGS` to a list of files when building a new image. Include
`src/user/init/console.conf` in that list to keep the default console.
