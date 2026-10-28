/* memdump ADDR SIZE: write SIZE bytes of physical memory at ADDR to stdout through an mmap of /dev/mem (read() fails on no-map regions) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
int main(int argc, char **argv)
{
	if (argc != 3)
		return 2;
	unsigned long a = strtoul(argv[1], 0, 0), n = strtoul(argv[2], 0, 0);
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) { perror("open"); return 1; }
	void *p = mmap(0, n, PROT_READ, MAP_SHARED, fd, a);
	if (p == MAP_FAILED) { perror("mmap"); return 1; }
	unsigned char *b = malloc(n);
	for (unsigned long i = 0; i < n; i += 4)
		*(unsigned int *)(b + i) = *(volatile unsigned int *)((char *)p + i);
	fwrite(b, 1, n, stdout);
	return 0;
}
