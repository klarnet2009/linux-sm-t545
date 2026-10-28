// SPDX-License-Identifier: GPL-2.0-only
// kexec_t545 <entry-hex> <dest-hex>:<file> [<dest-hex>:<file> ...] [dry]
// Load files as kexec segments at fixed physical addresses and boot them (reboot(LINUX_REBOOT_CMD_KEXEC)).
// Used to start mainline Linux on the SM-T545 from the running Android 4.9 kernel: segments = Image, DTB, initramfs and a
// tiny purgatory (x0 = DTB, jump to the Image). Freestanding aarch64 Linux, no libc.
typedef unsigned long u64;

struct seg { const void *buf; u64 bufsz; u64 mem; u64 memsz; };

static long sc(long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
	return x0;
}

static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc(64, 1, (long)s, slen(s), 0, 0, 0); }
static void outhex(u64 v)
{
	char b[19] = "0x0000000000000000";
	for (int i = 0; i < 16; i++)
		b[17 - i] = "0123456789abcdef"[(v >> (i * 4)) & 15];
	sc(64, 1, (long)b, 18, 0, 0, 0);
}
static u64 parsehex(const char *s)
{
	u64 v = 0;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	for (; *s; s++) {
		char c = *s;
		v <<= 4;
		v |= c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
	}
	return v;
}

__attribute__((used)) void main_c(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	struct seg segs[8];
	int n = 0, dry = 0;

	if (argc < 3) {
		out("usage: kexec_t545 entry dest:file ... [dry]\n");
		sc(93, 2, 0, 0, 0, 0, 0);
	}
	u64 entry = parsehex(argv[1]);
	for (int a = 2; a < argc && n < 8; a++) {
		char *arg = argv[a];
		if (arg[0] == 'd' && arg[1] == 'r' && arg[2] == 'y') {
			dry = 1;
			continue;
		}
		char *colon = arg;
		while (*colon && *colon != ':')
			colon++;
		if (!*colon) {
			out("bad segment: ");
			out(arg);
			out("\n");
			sc(93, 2, 0, 0, 0, 0, 0);
		}
		*colon = 0;
		u64 dest = parsehex(arg);
		char *path = colon + 1;
		long fd = sc(56, -100, (long)path, 0, 0, 0, 0);	/* openat(AT_FDCWD, path, O_RDONLY) */
		if (fd < 0) {
			out("open failed: ");
			out(path);
			out("\n");
			sc(93, 3, 0, 0, 0, 0, 0);
		}
		long size = sc(62, fd, 0, 2, 0, 0, 0);		/* lseek(SEEK_END) */
		sc(62, fd, 0, 0, 0, 0, 0);
		u64 memsz = ((u64)size + 4095) & ~4095UL;
		char *buf = (char *)sc(222, 0, memsz, 3, 0x22, -1, 0);	/* mmap anonymous */
		if ((long)buf < 0 && (long)buf > -4096) {
			out("mmap failed\n");
			sc(93, 4, 0, 0, 0, 0, 0);
		}
		long got = 0;
		while (got < size) {
			long r = sc(63, fd, (long)buf + got, size - got, 0, 0, 0);
			if (r <= 0) {
				out("read failed\n");
				sc(93, 5, 0, 0, 0, 0, 0);
			}
			got += r;
		}
		sc(57, fd, 0, 0, 0, 0, 0);
		segs[n].buf = buf;
		segs[n].bufsz = (u64)size;
		segs[n].mem = dest;
		segs[n].memsz = memsz;
		n++;
		out("segment ");
		outhex(dest);
		out(" size ");
		outhex((u64)size);
		out("\n");
	}
	long r = sc(104, entry, n, (long)segs, 0, 0, 0);	/* kexec_load(entry, n, segs, KEXEC_ARCH_DEFAULT) */
	out("kexec_load returned ");
	outhex((u64)r);
	out("\n");
	if (r != 0)
		sc(93, 6, 0, 0, 0, 0, 0);
	if (dry) {
		out("dry run, not rebooting\n");
		sc(93, 0, 0, 0, 0, 0, 0);
	}
	out("rebooting into the new kernel\n");
	sc(142, 0xfee1dead, 672274793, 0x45584543, 0, 0, 0);	/* LINUX_REBOOT_CMD_KEXEC */
	out("reboot returned\n");
	sc(93, 7, 0, 0, 0, 0, 0);
}

__asm__(
	".globl _start\n"
	"_start:\n"
	"	mov	x0, sp\n"
	"	bl	main_c\n"
	"	b	.\n"
);
