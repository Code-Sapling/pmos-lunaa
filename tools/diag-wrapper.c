/* PID 1 wrapper: dump state, then run pmOS's real init as a child with its
 * stdout/stderr wired into /dev/kmsg so its messages land in the ring buffer.
 * Re-dump periodically. Static, non-PIE. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>

static char buf[3 << 20];
static int n;
static char devpath[64];

static void P(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	n += vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
	va_end(ap);
}
static void cat(const char *path)
{
	int fd = open(path, O_RDONLY); int r;
	if (fd < 0) { P("  (no %s)\n", path); return; }
	while ((r = read(fd, buf + n, 8192)) > 0 && n < (int)sizeof(buf) - 65536) n += r;
	close(fd);
	if (n && buf[n-1] != '\n') buf[n++] = '\n';
}
static void listdir(const char *path)
{
	DIR *d = opendir(path); struct dirent *e;
	if (!d) { P("  (no dir %s)\n", path); return; }
	while ((e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
			P("  %s\n", e->d_name);
	closedir(d);
}
static void nap(int s) { struct timespec t = { s, 0 }; nanosleep(&t, 0); }

static void find_dev(void)
{
	DIR *d = opendir("/sys/class/block"); struct dirent *e;
	char p[512], ue[4096];
	if (d) {
		while ((e = readdir(d))) {
			snprintf(p, sizeof p, "/sys/class/block/%s/uevent", e->d_name);
			int fd = open(p, O_RDONLY); if (fd < 0) continue;
			int k = read(fd, ue, sizeof ue - 1); close(fd);
			if (k <= 0) continue; ue[k] = 0;
			if (strstr(ue, "PARTNAME=dtbo_a"))
				snprintf(devpath, sizeof devpath, "/dev/%s", e->d_name);
		}
		closedir(d);
	}
	if (!devpath[0]) strcpy(devpath, "/dev/sde15");
}

static void dump(const char *tag)
{
	n = 0;
	P("LUNAALOG-Q %s\n", tag);
	P("=== /sys/class/udc ===\n");   listdir("/sys/class/udc");
	P("=== /proc/net/dev ===\n");    cat("/proc/net/dev");
	P("=== /config|/sys/kernel/config/usb_gadget ===\n");
	listdir("/sys/kernel/config/usb_gadget");
	P("=== /dev (first level) ===\n"); listdir("/dev");
	P("=== deferred probe ===\n");   cat("/sys/kernel/debug/devices_deferred");
	P("=== DMESG ===\n");
	int r = syscall(SYS_syslog, 3, buf + n, (int)(sizeof(buf) - n - 1));
	if (r > 0) n += r; else P("(syslog failed)\n");
	P("\n=== END LUNAALOG %s ===\n", tag);

	int fd = open(devpath, O_WRONLY);
	if (fd >= 0) { write(fd, buf, n); fsync(fd); close(fd); }
}

int main(void)
{
	mkdir("/proc", 0755); mkdir("/sys", 0755); mkdir("/dev", 0755);
	mount("proc", "/proc", "proc", 0, 0);
	mount("sysfs", "/sys", "sysfs", 0, 0);
	mount("devtmpfs", "/dev", "devtmpfs", 0, 0);
	nap(6);
	find_dev();
	dump("STAGE-PRE");

	pid_t pid = fork();
	if (pid == 0) {
		int k = open("/dev/kmsg", O_WRONLY);
		if (k >= 0) { dup2(k, 1); dup2(k, 2); if (k > 2) close(k); }
		char *av[] = { "/init", 0 };
		char *ev[] = { "PATH=/usr/bin:/bin:/usr/sbin:/sbin", 0 };
		execve("/init.pmos", av, ev);
		/* if we get here pmOS's init could not be executed at all */
		const char *m = "<3>LUNAA: execve(/init.pmos) FAILED\n";
		int f = open("/dev/kmsg", O_WRONLY);
		if (f >= 0) { write(f, m, strlen(m)); close(f); }
		_exit(1);
	}

	nap(30); dump("STAGE-30s");
	nap(30); dump("STAGE-60s");
	nap(60); dump("STAGE-120s");
	for (;;) nap(30);
}
