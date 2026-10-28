// SM-T545 mainline spike, stage 1: chain loader.
// Samsung's bootloader only accepts a boot image whose appended DTB is the stock one (it applies the dtbo overlays
// to it), so the real kernel travels inside this "kernel": [this stub][Linux Image][our DTB][initramfs].
// The stub shows what the bootloader handed over, copies the three blobs to fixed addresses and jumps into Linux
// at EL1 with x0 = our DTB, MMU and D-cache off, as Documentation/arch/arm64/booting.rst requires.
// Freestanding aarch64, position independent (adrp/adr only), strict alignment (MMU is off at entry).
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

#include "font8x16.h"
#include "groups.h"

#ifndef BISECT_ID
#define BISECT_ID 1u
#endif

#ifndef BUILD_ID
#define BUILD_ID dev
#endif
#ifndef STUB_AREA
#define STUB_AREA 0x20000UL
#define KOFF 0x20000UL
#define KSZ 0x2200000UL		/* Image file size, padded */
#define KIMGSZ 0x2300000UL	/* image_size from the Image header (with bss) */
#define DOFF 0x2220000UL
#define DSZ 0x20000UL
#define ROFF 0x2240000UL
#define RSZ 0x100000UL
#define IMG_SIZE 0x2340000UL
#endif

#define KDEST 0x80200000UL	/* 2 MB aligned, below everything the bootloader uses */
#define DDEST 0x84a00000UL
#define RDEST 0x84b00000UL
#define RELO  0x85000000UL	/* where the stub moves itself to while it overwrites its own load area */

#define FB_BASE 0x9c000000UL
#define FB_W 1200
#define FB_H 1920
static unsigned sc = 2;
#define SC sc
#define TOP 48
#define COLS (FB_W / (8 * sc))
#define ROWS ((FB_H - TOP) / (16 * sc))
#define NB 1
#define FB_BYTES (FB_W * FB_H * 4UL)

#define STR_(x) #x
#define STR(x) STR_(x)

static volatile u32 *const fbs[3] = {
	(volatile u32 *)FB_BASE,
	(volatile u32 *)(FB_BASE + FB_BYTES),
	(volatile u32 *)(FB_BASE + 2 * FB_BYTES),
};
static unsigned cx, cy;
static u32 fg = 0xffffffffu, bg = 0xff000000u;
static u32 stage;
static u32 quiet = 1;	/* boot time: draw nothing (set in main(); initialised so that it lives in .data) */

void *memset(void *d, int c, unsigned long n) { volatile u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { volatile u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }

void mv64(u64 dst, u64 src, u64 len);
void cache_flush(u64 start, u64 len);
void relocate_and_run(u64 base, u64 relo, u64 len);

static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int sstart(const char *a, const char *pre) { while (*pre) { if (*a++ != *pre++) return 0; } return 1; }
static int sane(u64 a) { return a >= 0x80000000UL && a < 0x200000000UL; }

static void cell(unsigned col, unsigned row, u8 ch)
{
	const u8 *g = &font[ch * 16];
	if (quiet)
		return;
	unsigned x0 = col * 8 * SC, y0 = row * 16 * SC + TOP;
	for (unsigned y = 0; y < 16; y++) {
		u8 bits = g[y];
		for (unsigned sy = 0; sy < SC; sy++) {
			for (unsigned b = 0; b < NB; b++) {
				volatile u32 *line = fbs[b] + (y0 + y * SC + sy) * FB_W + x0;
				for (unsigned x = 0; x < 8; x++) {
					u32 c = (bits & (0x80 >> x)) ? fg : bg;
					for (unsigned sx = 0; sx < SC; sx++)
						line[x * SC + sx] = c;
				}
			}
		}
	}
}

static void pc(char ch)
{
	if (ch == '\n') { cx = 0; cy++; return; }
	if (cx >= COLS) { cx = 0; cy++; }
	if (cy >= ROWS) return;
	cell(cx++, cy, (u8)ch);
}
static void ps(const char *s) { while (*s) pc(*s++); }
static void pn(const char *s, unsigned max) { while (*s && max--) pc(*s++); }
static void px(u64 v, int digits) { for (int i = digits - 1; i >= 0; i--) pc("0123456789abcdef"[(v >> (i * 4)) & 15]); }
static void pd(u64 v)
{
	char b[24];
	int n = 0;
	if (!v) { pc('0'); return; }
	while (v) { b[n++] = '0' + v % 10; v /= 10; }
	while (n) pc(b[--n]);
}

/* rect into one buffer or (b == NB) into all of them */
static void rect_b(unsigned b, unsigned x, unsigned y, unsigned w, unsigned h, u32 c)
{
	if (quiet)
		return;
	for (unsigned j = 0; j < h; j++) {
		volatile u32 *l = fbs[b] + (y + j) * FB_W + x;
		for (unsigned i = 0; i < w; i++)
			l[i] = c;
	}
}
static void rect(unsigned x, unsigned y, unsigned w, unsigned h, u32 c)
{
	for (unsigned b = 0; b < NB; b++)
		rect_b(b, x, y, w, h, c);
}

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

/* the virtual counter: always readable at EL1 (under Android the hypervisor traps CNTPCT_EL0) */
static u64 now(void) { u64 v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }

static u64 initrd_s, initrd_e;

static void pval(const u8 *v, u32 len)
{
	int str = len > 1 && v[len - 1] == 0;
	for (u32 i = 0; str && i + 1 < len; i++)
		if (v[i] < 32 || v[i] > 126)
			str = 0;
	if (str) { ps("\""); pn((const char *)v, 100); ps("\""); return; }
	for (u32 i = 0; i < len / 4 && i < 8; i++) { px(be32(v + i * 4), 8); pc(' '); }
	if (len / 4 > 8)
		ps("...");
}

static void dump_fdt(const u8 *d)
{
	if (be32(d) != 0xd00dfeedu) {
		fg = 0xffff6060u; ps("DTB: bad magic "); px(be32(d), 8); ps("\n"); fg = 0xffffffffu;
		return;
	}
	u32 total = be32(d + 4), so = be32(d + 8), ss = be32(d + 12), ver = be32(d + 20);
	ps("DTB size "); pd(total); ps(" version "); pd(ver); ps("\n");
	const u8 *p = d + so;
	const char *strs = (const char *)(d + ss);
	int depth = 0;
	const char *n1 = 0, *n2 = 0;
	for (;;) {
		if (p >= d + total) { ps("(walk ran past DTB end)\n"); break; }
		u32 t = be32(p);
		p += 4;
		if (t == 1) {
			const char *name = (const char *)p;
			p += (slen(name) + 1 + 3) & ~3u;
			depth++;
			if (depth == 2) n1 = name;
			else if (depth == 3) n2 = name;
		} else if (t == 2) {
			depth--;
		} else if (t == 3) {
			u32 len = be32(p), no = be32(p + 4);
			p += 8;
			const u8 *val = p;
			p += (len + 3) & ~3u;
			const char *pname = strs + no;
			if (depth == 1 && (seq(pname, "model") || seq(pname, "qcom,msm-id") || seq(pname, "qcom,board-id"))) {
				fg = 0xff80ff80u; ps("root "); ps(pname); ps(" = "); pval(val, len); ps("\n"); fg = 0xffffffffu;
			} else if (depth == 2 && n1 && sstart(n1, "chosen") && !seq(pname, "bootargs")) {
				fg = 0xffffe080u; ps("chosen "); ps(pname); ps(" = "); pval(val, len); ps("\n"); fg = 0xffffffffu;
				if (seq(pname, "linux,initrd-start"))
					initrd_s = len == 8 ? ((u64)be32(val) << 32 | be32(val + 4)) : be32(val);
				if (seq(pname, "linux,initrd-end"))
					initrd_e = len == 8 ? ((u64)be32(val) << 32 | be32(val + 4)) : be32(val);
			} else if (depth == 2 && n1 && sstart(n1, "memory") && seq(pname, "reg")) {
				ps("memory "); pval(val, len); ps("\n");
			}
		} else if (t == 4) {
			/* nop */
		} else {
			break;
		}
	}
}

static u32 rd32(const volatile u8 *p) { return *(const volatile u32 *)p; }

/* The previous boot's console log lives in the ramoops console zone, in the layout both Linux 4.9 and 6.x use. */
static void dump_ramoops(void)
{
	const volatile u8 *z = (const volatile u8 *)0xa1340000UL;
	const volatile u8 *d = z + 12;
	const u32 cap = 0x40000 - 12;
	u32 sig = rd32(z), start = rd32(z + 4), size = rd32(z + 8);
	u32 len, end;

	fg = 0xff60ffffu;
	ps("ramoops console zone a1340000: sig "); px(sig, 8); ps(" start "); pd(start); ps(" size "); pd(size); ps("\n");
	fg = 0xffffffffu;
	if (sig == 0x43474244u && size <= cap && start <= cap) {
		len = size < 5200 ? size : 5200;
		end = start;
	} else {
		u32 p = cap;
		while (p && d[p - 1] == 0)
			p--;
		end = p;
		len = end < 5200 ? end : 5200;
		ps("(header invalid, showing the tail before the last non-zero byte)\n");
	}
	/* the last MAXL lines, each cut to one screen row (no wrapping, so everything stays readable at scale 2) */
	{
		const unsigned MAXL = 20;
		u32 lines = 0, from = len;
		if (len > 12000)
			len = 12000;
		from = len;
		while (from > 0) {
			u8 ch = d[(end + cap - len + from - 1) % cap];
			if (ch == '\n' && from != len) {
				if (++lines == MAXL)
					break;
			}
			from--;
		}
		unsigned col = 0, skip = 0;
		for (u32 i = from; i < len; i++) {
			u8 ch = d[(end + cap - len + i) % cap];
			if (ch == '\n') {
				if (i != from || col)
					pc('\n');
				col = 0;
				skip = 0;
			} else if (!skip) {
				pc(ch >= 32 && ch < 127 ? (char)ch : '.');
				if (++col >= COLS - 1)
					skip = 1;
			}
		}
		ps("\n");
	}
}

/* ---- bring-up bisect: the state lives in a reserved DRAM page that survives warm resets ---- */
#define SCRATCH 0xa0f00000UL
#define S_MAGIC 0x54353435u
#define ST_BASE 0xffu	/* attempt = all groups off */
#define ST_FINAL 0xfeu	/* attempt = the best set found */
#define ST_NEW 0xfdu	/* nothing tried yet */

/* does the previous boot's console log contain the needle? (it is written by our initramfs once userspace runs) */
static int log_has(const char *needle)
{
	const volatile u8 *z = (const volatile u8 *)0xa1340000UL;
	const volatile u8 *d = z + 12;
	const u32 cap = 0x40000 - 12;
	u32 sig = rd32(z), start = rd32(z + 4), size = rd32(z + 8);
	unsigned n = slen(needle);

	if (sig != 0x43474244u || size > cap || start > cap || size < n)
		return 0;
	u32 idx0 = (start + cap - size) % cap;
	for (u32 k = 0; k + n <= size; k++) {
		u32 idx = idx0 + k;
		if (idx >= cap)
			idx -= cap;
		if (d[idx] != (u8)needle[0])
			continue;
		unsigned j = 1;
		for (; j < n; j++) {
			u32 i2 = idx + j;
			if (i2 >= cap)
				i2 -= cap;
			if (d[i2] != (u8)needle[j])
				break;
		}
		if (j == n)
			return 1;
	}
	return 0;
}

/*
 * is the needle among the last win bytes of the previous boot's console log, with at most maxerr flipped bits? The zone comes
 * through the reset with flipped bits, sometimes many (seen: "rebONt: Rst`rtin\xa6 sYspem", ~11 bits off; unrelated text is 60+)
 */
static unsigned popc8(u8 v)
{
	unsigned n = 0;
	for (; v; v >>= 1)
		n += v & 1;
	return n;
}
static int log_tail_has(const char *needle, unsigned maxerr, u32 win)
{
	const volatile u8 *z = (const volatile u8 *)0xa1340000UL;
	const volatile u8 *d = z + 12;
	const u32 cap = 0x40000 - 12;
	u32 start = rd32(z + 4), size = rd32(z + 8);
	unsigned n = slen(needle);

	if (size > cap || start > cap || size < n)
		return 0;
	if (win > size)
		win = size;
	for (u32 k = 0; k + n <= win; k++) {
		u32 idx = (start + cap - win + k) % cap;
		unsigned err = 0;
		for (unsigned j = 0; j < n && err <= maxerr; j++) {
			u32 i2 = idx + j;
			if (i2 >= cap)
				i2 -= cap;
			err += popc8(d[i2] ^ (u8)needle[j]);
		}
		if (err <= maxerr)
			return 1;
	}
	return 0;
}

/* keep the last ~1000 bytes of the dead boot's console for the final report (scratch + 0x1000 + g * 0x400) */
static void save_tail(u32 g)
{
	volatile u8 *dst = (volatile u8 *)(SCRATCH + 0x1000 + g * 0x400);
	const volatile u8 *z = (const volatile u8 *)0xa1340000UL;
	const volatile u8 *d = z + 12;
	const u32 cap = 0x40000 - 12;
	u32 sig = rd32(z), start = rd32(z + 4), size = rd32(z + 8);
	u32 len = 0;

	if (sig == 0x43474244u && size <= cap && start <= cap) {
		len = size < 1000 ? size : 1000;
		for (u32 i = 0; i < len; i++)
			dst[4 + i] = d[(start + cap - len + i) % cap];
	} else {
		static const char msg[] = "(no ramoops log at all: the kernel died before the log was initialised)\n";
		for (; msg[len]; len++)
			dst[4 + len] = (u8)msg[len];
	}
	*(volatile u32 *)dst = len;
}

/* print the last n lines of a saved tail, each cut to one screen row */
static void show_saved(u32 g, unsigned n)
{
	const volatile u8 *src = (const volatile u8 *)(SCRATCH + 0x1000 + g * 0x400);
	u32 len = *(const volatile u32 *)src;
	const volatile u8 *d = src + 4;
	u32 from = len, lines = 0;

	if (len > 1000)
		len = from = 0;
	while (from > 0) {
		if (d[from - 1] == '\n' && from != len) {
			if (++lines == n)
				break;
		}
		from--;
	}
	unsigned col = 0, skip = 0;
	for (u32 i = from; i < len; i++) {
		u8 ch = d[i];
		if (ch == '\n') {
			if (col)
				pc('\n');
			col = 0;
			skip = 0;
		} else if (!skip) {
			pc(ch >= 32 && ch < 127 ? (char)ch : '.');
			if (++col >= COLS - 1)
				skip = 1;
		}
	}
	if (col)
		pc('\n');
}

static void patch_dtb(u8 *dtb, u32 mask, u32 bisect)
{
	static const u8 okay[9] = { 'o', 'k', 'a', 'y', 0, 0, 0, 0, 0 };

	for (unsigned g = 0; g < NGROUPS; g++) {
		if (!(mask & (1u << g)))
			continue;
		for (unsigned i = gstart[g]; i < gstart[g + 1]; i++)
			for (unsigned j = 0; j < 9; j++)
				dtb[goff[i] + j] = okay[j];
	}
	dtb[BISECT_OFF] = 0;
	dtb[BISECT_OFF + 1] = 0;
	dtb[BISECT_OFF + 2] = 0;
	dtb[BISECT_OFF + 3] = (u8)bisect;
}

/* TLMM pad n: CFG at 0x03400000 + 0x1000 * n (bit 9 = output enable), IN_OUT at +4 (bit 0 in, bit 1 out) */
static volatile u32 *tlmm(unsigned n) { return (volatile u32 *)(0x03400000UL + 0x1000UL * n); }
static void gpio_out(unsigned n, unsigned v)
{
	volatile u32 *r = tlmm(n);
	r[1] = v ? 2 : 0;
	r[0] = 1u << 9;
}

/* progress trace into the ramoops console zone: survives a reset and shows up in /sys/fs/pstore afterwards */
static void tr(const char *s)
{
	volatile u8 *z = (volatile u8 *)0xa1340000UL;
	volatile u32 *h = (volatile u32 *)z;
	const u32 cap = 0x40000 - 12;
	if (h[0] != 0x43474244u) { h[0] = 0x43474244u; h[1] = 0; h[2] = 0; }
	u32 start = h[1], size = h[2];
	while (*s) {
		z[12 + start] = (u8)*s++;
		if (++start >= cap)
			start = 0;
		if (size < cap)
			size++;
	}
	h[1] = start;
	h[2] = size;
}
static void trx(u64 v)
{
	char b[19];
	b[0] = '0'; b[1] = 'x'; b[18] = 0;
	for (int i = 0; i < 16; i++)
		b[17 - i] = "0123456789abcdef"[(v >> (i * 4)) & 15];
	tr(b);
}

extern char _image_end[], _start[];

static void line(const char *k, u64 v, int digits) { ps(k); ps(" "); px(v, digits); ps("\n"); }

static void wait_s(u64 frq, unsigned s)
{
	u64 t0 = now();
	while (now() - t0 < frq * s)
		;
}

void chain2(u64 base)
{
	/* running at RELO from here on */
	fg = 0xffffe080u;
	tr("stub: stage2 running at "); trx((u64)chain2); tr(" base "); trx(base); tr("\n");
	ps("stage2 at "); px((u64)chain2, 16); ps(", payload at "); px(base, 16); ps("\n");
	u64 t0 = now();
	mv64(DDEST, base + DOFF, DSZ);
	mv64(RDEST, base + ROFF, RSZ);
	tr("stub: dtb+initrd copied\n");
	ps("dtb+initrd copied\n");
	mv64(KDEST, base + KOFF, KSZ);
	u64 t1 = now();
	tr("stub: kernel copied\n");
	ps("kernel copied, ticks "); pd(t1 - t0); ps("\n");
	cache_flush(KDEST, KIMGSZ > KSZ ? KIMGSZ : KSZ);
	cache_flush(DDEST, DSZ);
	cache_flush(RDEST, RSZ);
	tr("stub: caches flushed, jumping into Linux at "); trx(now()); tr(" copy ticks "); trx(t1 - t0); tr("\n");
	ps("caches flushed, jumping to "); px(KDEST, 16); ps(" with x0 = "); px(DDEST, 16); ps("\n");
	rect(0, 44, FB_W, 4, 0xff00ff00u);	/* green line = the stub got this far */
	register u64 r0 __asm__("x0") = DDEST;
	register u64 r4 __asm__("x4") = KDEST;
	__asm__ volatile(
		"msr	daifset, #0xf\n"
		"ic	iallu\n"
		"dsb	sy\n"
		"isb\n"
		"mov	x1, xzr\n"
		"mov	x2, xzr\n"
		"mov	x3, xzr\n"
		"br	x4\n"
		:: "r"(r0), "r"(r4));
	for (;;)
		;
}

void main(u64 x0, u64 x1, u64 x2, u64 x3, u64 pcv)
{
	u64 el, midr, frq, sctlr = 0;
	/* .bss is not part of the loaded file and the bootloader does not clear it */
	cx = 0; cy = 0; stage = 0; initrd_s = 0; initrd_e = 0;
	u64 t_start = now(), t_sm, t_go;
	/*
	 * boot time: after a clean reboot or power-off of the previous boot, or a cold start (no console log at all), the stub
	 * draws nothing (the bootloader's logo stays up, the uncached framebuffer writes cost ~0.7 s) and does not wait; after a
	 * panic, hang or watchdog reset its screen with the previous boot's log tail stays up for 20 s
	 */
	int clean = rd32((const volatile u8 *)0xa1340000UL) != 0x43474244u ||
		    log_tail_has("reboot: Restarting system", 24, 4096) || log_tail_has("reboot: Power down", 16, 4096);
#ifdef FORCE_A
	quiet = clean;
#else
	quiet = 0;	/* bring-up searches: always show the screen */
#endif
	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	el >>= 2;
	__asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	if (el == 1)
		__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	else if (el == 2)
		__asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));

	rect(0, 0, FB_W, FB_H, bg);
	/* the bar thickness tells which of the three buffers the display really scans out: 20, 40 or 60 px */
	for (unsigned b = 0; b < NB; b++) {
		rect_b(b, 0, 0, 300, 20 * (b + 1), 0xffff0000u);
		rect_b(b, 300, 0, 300, 20 * (b + 1), 0xff00ff00u);
		rect_b(b, 600, 0, 300, 20 * (b + 1), 0xff0000ffu);
		rect_b(b, 900, 0, 300, 20 * (b + 1), 0xffffffffu);
	}

	fg = 0xff60ffffu;
	ps("SM-T545 stage1 (build " STR(BUILD_ID) ") - chain loader for mainline Linux\n");
	fg = 0xffffffffu;
	ps("EL "); pd(el); ps("  entry "); px(pcv, 9); ps("  x0 "); px(x0, 9); ps("  x2 "); px(x2, 6); ps("\n");
	stage = 3;
	(void)sctlr; (void)midr; (void)t_start; (void)x1; (void)x3;

	t_sm = now();
	/* --- bisect state machine --- */
	volatile u32 *st = (volatile u32 *)SCRATCH;
	u32 mask = 0, bisect = 1, cur;
	int prev_ok = -1;
	u32 prev = ST_NEW;
#ifdef DOM_MODE
	/*
	 * Search for the TrustZone-protected clocks of gcc: the DTB carries a protected-clocks list with every index;
	 * the stub rewrites it on every boot (entry i = i keeps clock i out of the driver, 0xffffffff lets it register)
	 * and halves the failing ranges until single clocks are left.
	 */
	{
		volatile u32 *D = (volatile u32 *)(SCRATCH + 0x200);
		const u32 N = PROT_N;
		u32 lo, hi;
		int okp = -1;
		if (st[0] != S_MAGIC || st[6] != BISECT_ID) {
			st[0] = S_MAGIC; st[6] = BISECT_ID; st[1] = 0;
			for (unsigned i = 0; i < 160; i++)
				D[i] = 0;
			u32 sp = 0, l = ((N - 1) / 16) * 16;
			for (;;) {
				u32 h = l + 16 > N ? N : l + 16;
				D[32 + sp++] = (l << 16) | h;
				if (l == 0)
					break;
				l -= 16;
			}
			D[2] = sp;
			lo = 0xffff; hi = 0;
		} else {
			lo = D[0]; hi = D[1];
			okp = log_has("t545-init: alive 1");
			if (lo == 0xffff) {
				if (okp) D[3] &= ~1u; else D[3] |= 1u;
			} else if (lo != 0xfffe) {
				if (okp) {
					for (u32 i = lo; i < hi; i++)
						D[4 + i / 32] |= 1u << (i % 32);
				} else if (hi - lo == 1) {
					D[12 + lo / 32] |= 1u << (lo % 32);
					save_tail(lo);
				} else {
					u32 mid = (lo + hi) / 2;
					D[32 + D[2]++] = (mid << 16) | hi;
					D[32 + D[2]++] = (lo << 16) | mid;
				}
			}
			if (lo == 0xffff && (D[3] & 1)) {
				lo = 0xffff; hi = 0;
			} else if (D[2]) {
				D[2]--;
				u32 e = D[32 + D[2]];
				lo = e >> 16; hi = e & 0xffff;
			} else {
				lo = 0xfffe; hi = 0;
			}
		}
		D[0] = lo; D[1] = hi;
		st[1] = st[1] + 1;

		u32 U[8];
		for (unsigned w = 0; w < 8; w++)
			U[w] = D[4 + w];
		if (lo < 0xfffe)
			for (u32 i = lo; i < hi; i++)
				U[i / 32] |= 1u << (i % 32);
		u8 *dtb = (u8 *)(pcv + DOFF);
		for (u32 i = 0; i < N; i++) {
			u32 v = (U[i / 32] >> (i % 32)) & 1 ? 0xffffffffu : i;
			dtb[PROT_OFF + 4 * i] = (u8)(v >> 24);
			dtb[PROT_OFF + 4 * i + 1] = (u8)(v >> 16);
			dtb[PROT_OFF + 4 * i + 2] = (u8)(v >> 8);
			dtb[PROT_OFF + 4 * i + 3] = (u8)v;
		}
		bisect = lo != 0xfffe;
		mask = DOM_MASK;
		patch_dtb(dtb, mask, bisect);

		u32 nsafe = 0, nbad = 0;
		for (unsigned w = 0; w < 8; w++)
			for (unsigned b = 0; b < 32; b++) {
				nsafe += (D[4 + w] >> b) & 1;
				nbad += (D[12 + w] >> b) & 1;
			}
		fg = 0xff60ffffu;
		ps("gcc protected-clocks search, attempt "); pd(st[1]); ps(": ");
		if (lo == 0xffff) ps("baseline, all clocks protected\n");
		else if (lo == 0xfffe) ps("FINAL: safe clocks registered, bad ones protected\n");
		else { ps("testing clocks "); pd(lo); ps(".."); pd(hi - 1); ps("\n"); }
		fg = 0xffffffffu;
		if (okp >= 0) { ps("previous attempt "); ps(okp ? "reached userspace\n" : "HUNG before userspace\n"); }
		ps("safe clocks "); pd(nsafe); ps(" of "); pd(N); ps(",  bad "); pd(nbad); ps(",  ranges queued "); pd(D[2]); ps("\n");
		if (D[3] & 1) { fg = 0xffff6060u; ps("BASELINE FAILED: even with every clock protected the kernel hangs\n"); fg = 0xffffffffu; }
		fg = 0xffff6060u;
		ps("protected (bad) clock ids:");
		for (u32 i = 0; i < N; i++)
			if ((D[12 + i / 32] >> (i % 32)) & 1) { pc(' '); pd(i); }
		ps("\n");
		fg = 0xffffffffu;
		if (lo == 0xfffe) {
			for (u32 i = 0; i < N; i++)
				if ((D[12 + i / 32] >> (i % 32)) & 1) {
					fg = 0xffffe080u; ps("-- clock "); pd(i); ps(" hung; last lines:\n"); fg = 0xffffffffu;
					show_saved(i, 5);
				}
		} else {
			ps("previous boot, log tail:\n");
			dump_ramoops();
		}
		goto go;
	}
#endif
#ifdef FORCE_A
	/* two fixed configurations, alternating on every boot; the previous boot's log tail is shown each time */
	{
		u32 n, m;
		if (st[0] != S_MAGIC || st[6] != BISECT_ID) {
			st[0] = S_MAGIC; st[6] = BISECT_ID; st[1] = 0;
		}
		/*
		 * st[1] counts the boots since postboot (Android up) last reset it; the initramfs stays in its debug init from the 2nd.
		 * A clean reboot or power-off (e.g. a reboot while Android was still starting) does not mean that the boot died.
		 */
		if (clean || st[1] > 16)	/* (more than 16 is a flipped bit, not a count) */
			st[1] = 0;
		n = st[1];
		st[1] = n + 1;
		m = (n & 1) ? FORCE_B : FORCE_A;
		bisect = 0;
		mask = m;
		fg = 0xff60ffffu;
		ps("FORCED configs, run "); pd(n); ps(": this boot = config "); ps((n & 1) ? "B" : "A");
		ps(", groups mask "); px(m, 8); ps("\n");
		if (n)
			{ ps("previous boot (config "); ps((n & 1) ? "A" : "B"); ps(") tail:\n"); }
		fg = 0xffffffffu;
		dump_ramoops();
		patch_dtb((u8 *)(pcv + DOFF), m, 0);
		goto go;
	}
#endif
	if (st[0] != S_MAGIC || st[6] != BISECT_ID) {
		st[0] = S_MAGIC; st[6] = BISECT_ID; st[1] = 0; st[2] = ST_NEW; st[3] = 0; st[4] = 0; st[5] = 0;
	} else {
		prev = st[2];
		prev_ok = log_has("t545-init: alive 1");
		if (prev == ST_BASE) {
			if (prev_ok) st[5] &= ~1u; else st[5] |= 1u;
		} else if (prev < NGROUPS) {
			if (prev_ok) st[3] |= 1u << prev; else { st[4] |= 1u << prev; save_tail(prev); }
		}
	}
	u32 good = st[3], bad = st[4];
	u32 todo = ~(good | bad) & ((1u << NGROUPS) - 1);
	if (st[2] == ST_NEW || (st[2] == ST_BASE && (st[5] & 1))) {
		cur = ST_BASE; mask = 0;
	} else if (todo) {
		cur = 0;
		while (!(todo & (1u << cur)))
			cur++;
		mask = good | (1u << cur);
	} else {
		cur = ST_FINAL; mask = good; bisect = 0;
	}
	st[2] = cur;
	st[1] = st[1] + 1;

	fg = 0xff60ffffu;
	ps("bring-up bisect, attempt "); pd(st[1]); ps(": ");
	if (cur < NGROUPS) { ps("testing group "); ps(gname[cur]); }
	else if (cur == ST_BASE) ps("baseline, every group off");
	else ps("FINAL set (all good groups on)");
	ps("\n");
	fg = 0xffffffffu;
	if (prev_ok >= 0) {
		ps("previous attempt ");
		if (prev < NGROUPS) ps(gname[prev]); else if (prev == ST_BASE) ps("baseline"); else ps("final");
		ps(prev_ok ? " reached userspace\n" : " HUNG before userspace\n");
	}
	if (st[5] & 1) { fg = 0xffff6060u; ps("BASELINE FAILED to reach userspace - bisect cannot continue\n"); fg = 0xffffffffu; }
	for (unsigned g = 0; g < NGROUPS; g++) {
		u32 bit = 1u << g;
		fg = (good & bit) ? 0xff80ff80u : (bad & bit) ? 0xffff6060u : (cur == g) ? 0xffffe080u : 0xff909090u;
		ps(gname[g]);
		for (unsigned k = slen(gname[g]); k < 9; k++) pc(' ');
		ps((good & bit) ? ":OK  " : (bad & bit) ? ":BAD " : (cur == g) ? ":TEST" : ":--  ");
		if (g % 4 == 3) ps("\n");
	}
	ps("\n");
	fg = 0xffffffffu;
	if (cur == ST_FINAL) {
		for (unsigned g = 0; g < NGROUPS; g++) {
			if (!(bad & (1u << g)))
				continue;
			fg = 0xffff6060u; ps("-- "); ps(gname[g]); ps(" hung; its last log lines:\n"); fg = 0xffffffffu;
			show_saved(g, 7);
		}
	}
	patch_dtb((u8 *)(pcv + DOFF), mask, bisect);

	ps("previous boot, console log tail (ramoops):\n");
	dump_ramoops();

go:
	t_go = now();
	/*
	 * USB data path of the Tab Active Pro: GPIO 50 ("muic,mux_sel") selects the normal USB-C port (0) or the pogo
	 * pins (1), GPIO 98 is the SM5705 AFC switch. Samsung's kernel drives mux_sel to 0 at boot; nobody does that here.
	 */
	{
		u32 c50 = tlmm(50)[0], o50 = tlmm(50)[1], c98 = tlmm(98)[0], o98 = tlmm(98)[1];
		fg = 0xffffe080u;
		ps("TLMM before: gpio50 cfg "); px(c50, 8); ps(" io "); px(o50, 8); ps("  gpio98 cfg "); px(c98, 8); ps(" io "); px(o98, 8); ps("\n");
		fg = 0xffffffffu;
#ifdef GPIO50_VAL
		gpio_out(50, GPIO50_VAL);
#endif
#ifdef GPIO98_VAL
		gpio_out(98, GPIO98_VAL);
#endif
		ps("TLMM after:  gpio50 cfg "); px(tlmm(50)[0], 8); ps(" io "); px(tlmm(50)[1], 8); ps("  gpio98 cfg "); px(tlmm(98)[0], 8); ps(" io "); px(tlmm(98)[1], 8); ps("\n");
	}
	if (pcv < RELO + STUB_AREA && pcv + IMG_SIZE > DDEST) {
		fg = 0xffff6060u; ps("payload overlaps the fixed destinations - not jumping\n");
		for (;;)
			;
	}
	{	/* a fresh trace for the next boot: valid header, empty; the kernel appends its own log after zapping it */
		volatile u32 *h = (volatile u32 *)0xa1340000UL;
		/* keep the previous boot console (ring) so that a hang can be read from /sys/fs/pstore in the next boot; header only when invalid */
		if (h[0] != 0x43474244u) { h[0] = 0x43474244u; h[1] = 0; h[2] = 0; }
		tr("stub: build " STR(BUILD_ID) " entry pc "); trx(pcv); tr(" x0 "); trx(x0); tr("\n");
		tr("stub: ticks start "); trx(t_start); tr(" logic "); trx(t_sm); tr(" go "); trx(t_go); tr(" wait "); trx(now()); tr(clean ? " clean\n" : " not clean\n");
	}
	/* 20 s to read (photograph) this screen only after a boot that did not end cleanly (panic, hang, watchdog) */
	ps(clean ? "jumping ...\n" : "previous boot did not end in a clean reboot: 20 s to read this screen, then jumping ...\n");
	wait_s(frq, bisect ? 3 : clean ? 0 : 20);
	relocate_and_run(pcv, RELO, STUB_AREA);
	for (;;)
		;
}

void exc_c(u64 esr, u64 elr, u64 far, u64 idx, u64 spsr, u64 sp, u64 base)
{
	quiet = 0;
	tr("stub: EXCEPTION vec "); trx(idx); tr(" esr "); trx(esr); tr(" elr "); trx(elr); tr(" far "); trx(far); tr(" base "); trx(base); tr("\n");
	sc = 2;
	fg = 0xffff6060u;
	cx = 0; cy = ROWS - 8;
	rect(0, TOP + cy * 16 * SC, FB_W, 8 * 16 * SC, 0xff300000u);
	bg = 0xff300000u;
	ps("EXCEPTION vec "); pd(idx); ps(" stage "); pd(stage);
	ps("\nesr "); px(esr, 16);
	ps("\nelr "); px(elr, 16); ps("  far "); px(far, 16);
	ps("\nspsr "); px(spsr, 16); ps("  sp "); px(sp, 16);
	ps("\nbase "); px(base, 16); ps("  elr-base "); px(elr - base, 8);
	if (sane(elr)) { ps("\ninsn at elr "); px(*(volatile u32 *)elr, 8); }
	ps("\n");
	for (;;)
		;
}

__asm__(
	".section .text.head,\"ax\"\n"
	".globl _start\n"
	"_start:\n"
	"	b	1f\n"
	"	.word	0\n"
	"	.quad	0\n"
	"	.quad	" STR(IMG_SIZE) "\n"
	"	.quad	0xa\n"
	"	.quad	0, 0, 0\n"
	"	.word	0x644d5241\n"
	"	.word	0\n"
	"1:	mov	x20, x0\n"
	"	mov	x21, x1\n"
	"	mov	x22, x2\n"
	"	mov	x23, x3\n"
	"	adr	x24, _start\n"
	"	adrp	x9, stack_top\n"
	"	add	x9, x9, :lo12:stack_top\n"
	"	mov	sp, x9\n"
	"	adr	x10, vectors\n"
	"	mrs	x9, CurrentEL\n"
	"	lsr	x9, x9, #2\n"
	"	cmp	x9, #2\n"
	"	b.eq	2f\n"
	"	msr	vbar_el1, x10\n"
	"	b	3f\n"
	"2:	msr	vbar_el2, x10\n"
	"3:	mov	x0, x20\n"
	"	mov	x1, x21\n"
	"	mov	x2, x22\n"
	"	mov	x3, x23\n"
	"	mov	x4, x24\n"
	"	bl	main\n"
	"	b	.\n"
	"exc_entry:\n"
	"	mov	x5, sp\n"
	"	adr	x6, _start\n"
	"	adrp	x9, stack_top\n"
	"	add	x9, x9, :lo12:stack_top\n"
	"	mov	sp, x9\n"
	"	mrs	x9, CurrentEL\n"
	"	lsr	x9, x9, #2\n"
	"	cmp	x9, #2\n"
	"	b.eq	4f\n"
	"	mrs	x0, esr_el1\n"
	"	mrs	x1, elr_el1\n"
	"	mrs	x2, far_el1\n"
	"	mrs	x4, spsr_el1\n"
	"	b	5f\n"
	"4:	mrs	x0, esr_el2\n"
	"	mrs	x1, elr_el2\n"
	"	mrs	x2, far_el2\n"
	"	mrs	x4, spsr_el2\n"
	"5:	bl	exc_c\n"
	"	b	.\n"
	"	.balign	2048\n"
	"vectors:\n"
	"	.set	vi, 0\n"
	"	.rept	16\n"
	"	mov	x3, #vi\n"
	"	b	exc_entry\n"
	"	.balign	128\n"
	"	.set	vi, vi + 1\n"
	"	.endr\n"
	"	.section .data.stack,\"aw\"\n"
	"	.balign	16\n"
	"	.skip	0x8000\n"
	"stack_top:\n"
	"	.skip	16\n"
	"	.text\n"
	/* x0 = dst, x1 = src, x2 = len (multiple of 32); overlap-safe */
	".globl mv64\n"
	"mv64:\n"
	"	cmp	x0, x1\n"
	"	b.hi	20f\n"
	"10:	ldp	x3, x4, [x1], #16\n"
	"	ldp	x5, x6, [x1], #16\n"
	"	stp	x3, x4, [x0], #16\n"
	"	stp	x5, x6, [x0], #16\n"
	"	subs	x2, x2, #32\n"
	"	b.hi	10b\n"
	"	ret\n"
	"20:	add	x0, x0, x2\n"
	"	add	x1, x1, x2\n"
	"21:	ldp	x5, x6, [x1, #-16]!\n"
	"	ldp	x3, x4, [x1, #-16]!\n"
	"	stp	x5, x6, [x0, #-16]!\n"
	"	stp	x3, x4, [x0, #-16]!\n"
	"	subs	x2, x2, #32\n"
	"	b.hi	21b\n"
	"	ret\n"
	/* x0 = start, x1 = len: clean+invalidate to PoC */
	".globl cache_flush\n"
	"cache_flush:\n"
	"	mrs	x4, ctr_el0\n"
	"	ubfx	x4, x4, #16, #4\n"
	"	mov	x5, #4\n"
	"	lsl	x4, x5, x4\n"
	"	add	x1, x0, x1\n"
	"	sub	x5, x4, #1\n"
	"	bic	x0, x0, x5\n"
	"30:	dc	civac, x0\n"
	"	add	x0, x0, x4\n"
	"	cmp	x0, x1\n"
	"	b.lo	30b\n"
	"	dsb	sy\n"
	"	ret\n"
	/* x0 = current base, x1 = new base, x2 = length of the stub area: copy ourselves, move sp, continue in chain2() there */
	".globl relocate_and_run\n"
	"relocate_and_run:\n"
	"	mov	x19, x0\n"
	"	mov	x20, x1\n"
	"	mov	x0, x1\n"
	"	mov	x1, x19\n"
	"	bl	mv64\n"
	"	mov	x9, sp\n"
	"	sub	x9, x9, x19\n"
	"	add	x9, x9, x20\n"
	"	adr	x10, chain2\n"
	"	sub	x10, x10, x19\n"
	"	add	x10, x10, x20\n"
	"	dsb	sy\n"
	"	ic	iallu\n"
	"	dsb	sy\n"
	"	isb\n"
	"	mov	sp, x9\n"
	"	mov	x0, x19\n"
	"	br	x10\n"
);
