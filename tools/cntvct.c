/* time since the arch counter started (SoC reset) vs. CLOCK_BOOTTIME: the difference is what ran before the kernel */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
int main(void)
{
	uint64_t c, f;
	struct timespec ts;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(c));
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	clock_gettime(CLOCK_BOOTTIME, &ts);
	double up = ts.tv_sec + ts.tv_nsec / 1e9, cs = (double)c / f;
	printf("cntvct %llu Hz %llu counter %.3f s boottime %.3f s before kernel %.3f s\n",
	       (unsigned long long)c, (unsigned long long)f, cs, up, cs - up);
	return 0;
}
