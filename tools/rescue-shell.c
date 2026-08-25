/* Self-contained rescue init: brings up the USB gadget, assigns an IP, and
 * serves a shell on TCP 23 from its own code. Does NOT run pmOS's init, so
 * nothing else can tear the gadget down or kill our processes. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#define SLOT (512*1024)
static char buf[SLOT];
static int n, slot;
static char devpath[64], vbdev[64];

static void P(const char*f,...){va_list a;va_start(a,f);n+=vsnprintf(buf+n,sizeof(buf)-n-1,f,a);va_end(a);}
static void cat(const char*p){int fd=open(p,O_RDONLY),r;if(fd<0){P("  <no %s>\n",p);return;}
	while((r=read(fd,buf+n,4096))>0&&n<(int)sizeof(buf)-8192)n+=r;close(fd);
	if(n&&buf[n-1]!='\n')buf[n++]='\n';}
static void ls(const char*p){DIR*d=opendir(p);struct dirent*e;if(!d){P("  <no %s>\n",p);return;}
	while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))P("  %s\n",e->d_name);closedir(d);}
static int wr(const char*p,const char*v){int fd=open(p,O_WRONLY);if(fd<0)return -1;
	int r=(int)write(fd,v,strlen(v));close(fd);return r;}
static void klog(const char*m){int f=open("/dev/kmsg",O_WRONLY);if(f>=0){write(f,m,strlen(m));close(f);}}
static void nap(int s){struct timespec t={s,0};nanosleep(&t,0);}

#define CFG "/sys/kernel/config/usb_gadget"

static void gadget_setup(void)
{
	mkdir(CFG"/g1",0755);
	wr(CFG"/g1/idVendor","0x18D1"); wr(CFG"/g1/idProduct","0xD001");
	wr(CFG"/g1/bcdDevice","0x0100"); wr(CFG"/g1/bcdUSB","0x0200");
	mkdir(CFG"/g1/strings/0x409",0755);
	wr(CFG"/g1/strings/0x409/manufacturer","Realme");
	wr(CFG"/g1/strings/0x409/product","lunaa rescue");
	wr(CFG"/g1/strings/0x409/serialnumber","postmarketOS");
	mkdir(CFG"/g1/configs/c.1",0755);
	mkdir(CFG"/g1/configs/c.1/strings/0x409",0755);
	wr(CFG"/g1/configs/c.1/strings/0x409/configuration","rescue");

	const char *fns[] = { "ncm.usb0", "rndis.usb0", "ecm.usb0", 0 };
	char p[256], l[256];
	for (int i = 0; fns[i]; i++) {
		snprintf(p,sizeof p, CFG"/g1/functions/%s", fns[i]);
		if (mkdir(p,0755) == 0) {
			snprintf(l,sizeof l, CFG"/g1/configs/c.1/%s", fns[i]);
			symlink(p,l);
			break;
		}
	}
	/* bind to the real UDC */
	DIR *d = opendir("/sys/class/udc"); struct dirent *e;
	if (d) { while ((e=readdir(d))) {
		if (strstr(e->d_name,"dwc3")) { wr(CFG"/g1/UDC", e->d_name); break; }
	} closedir(d); }
}

static void force_peripheral(void)
{
	wr("/sys/bus/platform/devices/a600000.ssusb/mode","peripheral");
	wr("/sys/devices/platform/soc/a600000.ssusb/mode","peripheral");
	wr("/sys/bus/platform/devices/a600000.dwc3/mode","peripheral");
}

static void set_ip(const char *ifname)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) return;
	struct ifreq r; memset(&r,0,sizeof r);
	strncpy(r.ifr_name, ifname, IFNAMSIZ-1);
	struct sockaddr_in *a = (struct sockaddr_in *)&r.ifr_addr;
	a->sin_family = AF_INET;

	a->sin_addr.s_addr = inet_addr("172.16.42.1");
	ioctl(s, SIOCSIFADDR, &r);
	a->sin_addr.s_addr = inet_addr("255.255.255.0");
	ioctl(s, SIOCSIFNETMASK, &r);
	ioctl(s, SIOCGIFFLAGS, &r);
	r.ifr_flags |= IFF_UP | IFF_RUNNING;
	ioctl(s, SIOCSIFFLAGS, &r);
	close(s);
}

static void find_parts(void)
{
	DIR*d=opendir("/sys/class/block");struct dirent*e;char p[512],ue[4096];
	if(!d) return;
	while((e=readdir(d))){
		snprintf(p,sizeof p,"/sys/class/block/%s/uevent",e->d_name);
		int fd=open(p,O_RDONLY); if(fd<0) continue;
		int k=(int)read(fd,ue,sizeof ue-1); close(fd); if(k<=0) continue; ue[k]=0;
		if(strstr(ue,"PARTNAME=dtbo_a"))        snprintf(devpath,sizeof devpath,"/dev/%s",e->d_name);
		if(strstr(ue,"PARTNAME=vendor_boot_a")) snprintf(vbdev,sizeof vbdev,"/dev/%s",e->d_name);
	}
	closedir(d);
}

static void dump(const char *tag)
{
	n=0;
	P("LUNAALOG-W %s (slot %d)\n", tag, slot);
	P("=== udc ===\n");       ls("/sys/class/udc");
	P("UDC = ");             cat(CFG"/g1/UDC");
	P("mode = ");            cat("/sys/bus/platform/devices/a600000.ssusb/mode");
	P("=== g1/configs/c.1 ===\n"); ls(CFG"/g1/configs/c.1");
	P("=== net ===\n");      cat("/proc/net/dev");
	P("=== listening ===\n");cat("/proc/net/tcp");
	P("=== /dev/pts? ===\n");ls("/dev/pts");
	P("=== DMESG ===\n");
	int r=(int)syscall(SYS_syslog,3,buf+n,(int)(sizeof(buf)-n-1));
	if(r>0)n+=r; else P("(syslog failed)\n");
	P("\n=== END LUNAALOG %s ===\n",tag);
	for (int i=0;i<2;i++){
		const char *t = i ? vbdev : devpath;
		if(!t[0]) continue;
		int fd=open(t,O_WRONLY);
		if(fd>=0){lseek(fd,(off_t)slot*SLOT,SEEK_SET);write(fd,buf,n);fsync(fd);close(fd);}
	}
	slot++;
}

int main(void)
{
	signal(SIGCHLD, SIG_IGN);
	mkdir("/proc",0755); mkdir("/sys",0755); mkdir("/dev",0755);
	mount("proc","/proc","proc",0,0);
	mount("sysfs","/sys","sysfs",0,0);
	mount("devtmpfs","/dev","devtmpfs",0,0);
	mkdir("/dev/pts",0755);
	mount("devpts","/dev/pts","devpts",0,0);
	mkdir("/sys/kernel/config",0755);
	mount("configfs","/sys/kernel/config","configfs",0,0);
	klog("<3>LUNAA-W: init running\n");

	for (int i=0;i<15;i++){ force_peripheral(); nap(1); }
	gadget_setup();
	force_peripheral();
	nap(2);
	set_ip("usb0");
	klog("<3>LUNAA-W: gadget + ip configured\n");

	find_parts();

	/* our own shell server -- no busybox-extras, no telnetd */
	int ls_fd = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(ls_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in sa; memset(&sa,0,sizeof sa);
	sa.sin_family = AF_INET; sa.sin_port = htons(23);
	sa.sin_addr.s_addr = INADDR_ANY;
	if (bind(ls_fd,(struct sockaddr*)&sa,sizeof sa) < 0) klog("<3>LUNAA-W: bind FAILED\n");
	if (listen(ls_fd, 4) < 0) klog("<3>LUNAA-W: listen FAILED\n");
	klog("<3>LUNAA-W: listening on 23\n");

	if (fork() == 0) {                       /* periodic diagnostics */
		int t[]={10,20,30,60,60};
		char tag[16];
		for (int i=0;i<5;i++){ nap(t[i]); force_peripheral();
			snprintf(tag,sizeof tag,"T%d",i); dump(tag); }
		for(;;){ nap(30); force_peripheral(); }
	}

	for (;;) {
		int c = accept(ls_fd, 0, 0);
		if (c < 0) { nap(1); continue; }
		if (fork() == 0) {
			dup2(c,0); dup2(c,1); dup2(c,2);
			if (c > 2) close(c);
			char *av[] = { "sh", "-i", 0 };
			char *ev[] = { "PATH=/usr/bin:/bin:/usr/sbin:/sbin",
			               "HOME=/", "TERM=vt100", "PS1=lunaa# ", 0 };
			execve("/bin/busybox", (char*[]){"sh","-i",0}, ev);
			execve("/usr/bin/busybox", av, ev);
			const char *m = "exec of busybox failed\n";
			write(1, m, strlen(m));
			_exit(1);
		}
		close(c);
	}
}
