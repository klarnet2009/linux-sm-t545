// SPDX-License-Identifier: GPL-2.0-only
// reboot(LINUX_REBOOT_CMD_RESTART2, "download"): lets the qcom-pon reboot-mode driver store Samsung's download-mode
// reason (0x15) in the PMIC before the reset, so Samsung's bootloader enters Download mode. Freestanding aarch64.
static long sys4(long n, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}

void _start(void)
{
	static const char arg[] = "download";
	sys4(142, 0xfee1dead, 672274793, 0xA1B2C3D4, (long)arg);
	for (;;)
		;
}
