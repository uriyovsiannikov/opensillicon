#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern const unsigned char server_start[], server_end[];
#define CHECK(test) do { if (!(test)) { printf("X11_LOCAL_FAIL:%d errno=%d\n", __LINE__, errno); return 1; } } while (0)

static Display *
open_display(void)
{
 for (unsigned i = 0; i < 200; i++) {
  Display *display = XOpenDisplay(":0");
  if (display) return display;
  struct timespec delay = {0, 50000000};
  nanosleep(&delay, NULL);
 }
 return NULL;
}

int
main(void)
{
 FILE *file = fopen("/tmp/X", "wb");
 size_t length = (size_t)(server_end - server_start);
 CHECK(file && fwrite(server_start, 1, length, file) == length && !fclose(file));
 CHECK(chmod("/tmp/X", 0755) == 0);
 pid_t server = fork();
 CHECK(server >= 0);
 if (!server) {
  char *args[] = {"X", ":0", "-ac", NULL};
  execv("/tmp/X", args);
  _exit(127);
 }
 Display *display = open_display();
 CHECK(display);
 struct stat st;
 CHECK(stat("/tmp/.X11-unix/X0", &st) == 0 && S_ISSOCK(st.st_mode));
 int screen = DefaultScreen(display);
 Window window = XCreateSimpleWindow(display, RootWindow(display, screen),
     8, 8, 32, 32, 0, BlackPixel(display, screen), BlackPixel(display, screen));
 CHECK(window);
 XMapWindow(display, window);
 GC gc = XCreateGC(display, window, 0, NULL);
 CHECK(gc);
 XSetForeground(display, gc, WhitePixel(display, screen));
 XFillRectangle(display, window, gc, 0, 0, 32, 32);
 XSync(display, False);
 XImage *image = XGetImage(display, window, 0, 0, 32, 32, AllPlanes, ZPixmap);
 CHECK(image && XGetPixel(image, 16, 16) == WhitePixel(display, screen));
 XDestroyImage(image);
 XFreeGC(display, gc);
 XDestroyWindow(display, window);
 XCloseDisplay(display);
 struct timespec delay = {0, 200000000};
 nanosleep(&delay, NULL);
 display = open_display();
 CHECK(display);
 XSync(display, False);
 XCloseDisplay(display);
 CHECK(kill(server, SIGTERM) == 0);
 int status;
 CHECK(waitpid(server, &status, 0) == server);
 CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
 CHECK(stat("/tmp/.X11-unix/X0", &st) == -1 && errno == ENOENT);
 puts("X11_LOCAL_PASS");
 return 0;
}
