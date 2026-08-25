/* realme-lunaa boot shim.
 * Forces dwc3 into peripheral mode (USB gadget never enumerates otherwise),
 * then hands off to pmOS's init as PID 1. Nothing else -- the diagnostic
 * machinery from the bring-up phase is gone. */
#define _GNU_SOURCE
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mount.h>
#include <sys/stat.h>

static int wr(const char *p, const char *v)
{
	int fd = open(p, O_WRONLY);
	if (fd < 0) return -1;
	int r = (int)write(fd, v, strlen(v));
	close(fd);
	return r;
}

static int force_peripheral(void)
{
	int ok = 0;
	if (wr("/sys/bus/platform/devices/a600000.ssusb/mode", "peripheral") > 0) ok = 1;
	if (!ok && wr("/sys/devices/platform/soc/a600000.ssusb/mode", "peripheral") > 0) ok = 1;
	if (!ok && wr("/sys/bus/platform/devices/a600000.dwc3/mode", "peripheral") > 0) ok = 1;
	return ok;
}

static void nap_ms(long ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&t, 0);
}

int main(void)
{
	mkdir("/proc", 0755); mkdir("/sys", 0755); mkdir("/dev", 0755);
	mount("proc", "/proc", "proc", 0, 0);
	mount("sysfs", "/sys", "sysfs", 0, 0);
	mount("devtmpfs", "/dev", "devtmpfs", 0, 0);

	/* Vendor firmware (yupik_ipa_fws, vpu20_1v) lives on the Android vendor
	 * partition, which we do not mount. With the default 60s timeout each
	 * request_firmware() falls back to a userspace helper that does not exist
	 * in the initramfs and blocks the full 60s -- ~88s of boot, and the rootfs
	 * did not mount until 130s. Fail fast instead. */
	wr("/sys/class/firmware/timeout", "1");

	/* dwc3 binds during boot; poll briefly instead of sleeping a fixed 8s */
	for (int i = 0; i < 40; i++) {          /* <= 4s, exits as soon as it takes */
		if (force_peripheral()) break;
		nap_ms(100);
	}

	/* Re-assert a few times in the background: anything that rebinds the UDC
	 * can knock the controller back to idle. Then get out of the way. */
	if (fork() == 0) {
		for (int i = 0; i < 6; i++) { nap_ms(2000); force_peripheral(); }
		_exit(0);
	}

	execve("/init.pmos", (char *[]){ "/init", 0 },
	       (char *[]){ "PATH=/usr/bin:/bin:/usr/sbin:/sbin", 0 });
	for (;;) nap_ms(30000);
}
