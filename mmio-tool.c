/*
 * MMIO Tool for Wii Linux
 * Copyright (C) 2024-2025 Techflash.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

static void usage(void) {
	puts(
"./mmio-tool [mode] [address] <value>\n"
"Options:\n"
"	MODE:		Required.  Either 'r' or 'w', for read, or write.\n"
"\n"
"	ADDRESS:	Required.  Hexadecimal address to access.\n"
"\n"
"	VALUE:		Required for write, forbidden for read.\n"
"			The value to write to the provided address.\n"
"\n"
	);
}

static int addr2range(char *addr, uint32_t *_range, uint32_t *_off) {
	uint32_t val, off, range;
	char *tmp;

	val = strtoul(addr, &tmp, 16);
	errno = 0;
	if (tmp == addr || val == 0 || val == ULONG_MAX || errno == EINVAL || errno == ERANGE) {
		printf("ERROR: Invalid address \"%s\"\n", addr);
		usage();
		return 1;
	}
	if ((val & 3) != 0) {
		printf("ERROR: Misaligned address \"%s\" is not allowed\n", addr);
		usage();
		return 1;
	}
	if ((val & 0xC0000000) != 0) {
		puts("WARN: Attempting to touch address in SDK/libogc virtual range, fixing...");
		val &= ~0xC0000000;
	}

	range = val & 0x0FF00000;
	if (range != 0x0C000000 && /* Legacy Flipper registers */
	    range != 0x0D000000 && /* Hollywood registers */
	    range != 0x0D800000) { /* Hollywood registers (mirrored) */
		printf("ERROR: Refusing to touch unknown register range: 0x%08X!  Typo?\n", range);
		usage();
		return 1;
	}

	off = val & 0x000FFFFF;

	*_range = range;
	*_off = off;

	return 0;
}

int main(int argc, char *argv[]) {
	int fd, len, ret, mode;
	uint32_t *mem, *tmp, range, off, val;
	char *tmpStr;

	/* 1 region */
	len = 0x00f00000;

	/* do we have the right number of args? */
	if (argc < 3 || argc > 4) {
		usage();
		return 1;
	}

	/* valid mode? */
	if (strcmp(argv[1], "r") == 0)
		mode = 0;
	else if (strcmp(argv[1], "w") == 0)
		mode = 1;
	else {
		usage();
		return 1;
	}

	/* right args? */
	if ((mode == 1 && argc != 4) || (mode == 0 && argc != 3)) {
		usage();
		ret = 1;
		goto out;
	}

	/* setup */
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("ERROR: open failed");
		return 1;
	}
	if (addr2range(argv[2], &range, &off)) {
		close(fd);
		return 1;
	}

	mem = mmap(NULL, len,
		PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, range
	);
	if (mem == MAP_FAILED) {
		perror("ERROR: mmap failed");
		close(fd);
		return 1;
	}

	/* get the pointer to the val we want */
	tmp = mem + (off / sizeof(uint32_t));

	if (mode == 0) {
		val = *tmp;
		printf("0x%08X: %08X\n", range + off, val);
		ret = 0;
		goto out;
	}

	if (mode == 1) {
		errno = 0;
		val = strtoul(argv[3], &tmpStr, 16);
		if (tmpStr == argv[3] || errno == EINVAL || errno == ERANGE) {
			printf("ERROR: invalid value \"%s\"\n", argv[3]);
			usage();
			ret = 1;
			goto out;
		}
		*tmp = val;

		printf("Successfully wrote 0x%08X to 0x%08X\n", val, range + off);
		ret = 0;
		goto out;
	}

out:
	munmap(mem, len);
	close(fd);
	return ret;
}
