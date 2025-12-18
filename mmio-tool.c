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

#define ARG_IDX_MODE 1
#define ARG_IDX_LEN 2
#define ARG_IDX_ADDR 3
#define ARG_IDX_VAL 4

/* program, mode, length, address */
#define MIN_ARGS 4
/* program, mode, length, address, value */
#define MAX_ARGS 5


static void usage(void) {
	puts(
"./mmio-tool [mode] [address] [length] <value>\n"
"Options:\n"
"	MODE:		Required.  Either 'r' or 'w', for read, or write.\n"
"\n"
"	ADDRESS:	Required.  Hexadecimal address to access.\n"
"\n"
"	LENGTH:		Required.  Valid values: 1, 2, 4\n"
"\n"
"	VALUE:		Required for write, forbidden for read.\n"
"			The value to write to the provided address.\n"
"\n"
"\n"
"This is Wii-Linux mmio-tool v1.1.1\n"
	);
}

static int addr2range(char *addr, uint32_t *_range, uint32_t *_off, int len) {
	uint32_t val, off, range;
	char *tmp;

	val = strtoul(addr, &tmp, 16);
	errno = 0;
	if (tmp == addr || val == 0 || val == ULONG_MAX || errno == EINVAL || errno == ERANGE) {
		printf("ERROR: Invalid address \"%s\"\n", addr);
		usage();
		return 1;
	}
	if ((val & (len - 1)) != 0) {
		printf("ERROR: Misaligned address \"%s\" is not allowed\n", addr);
		usage();
		return 1;
	}
	if ((val & 0xC0000000) != 0) {
		puts("WARN: Attempting to touch address in SDK/libogc virtual range, fixing...");
		val &= ~0xC0000000;
	}

	range = val & 0x0FF00000;
	if (range != 0x08000000 && /* GX EFB */
	    range != 0x0C000000 && /* Legacy Flipper registers */
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
	int fd, len, mapLen, ret, mode;
	uint32_t *mem, *tmp, range, off, val;
	char *tmpStr;

	/* 1 region */
	mapLen = 0x00f00000;

	/* do we have the right number of args? */
	if (argc < MIN_ARGS || argc > MAX_ARGS) {
		usage();
		return 1;
	}

	/* valid mode? */
	if (strcmp(argv[ARG_IDX_MODE], "r") == 0)
		mode = 0;
	else if (strcmp(argv[ARG_IDX_MODE], "w") == 0)
		mode = 1;
	else {
		usage();
		return 1;
	}

	/* valid length? */
	len = strtoul(argv[ARG_IDX_LEN], &tmpStr, 10);
	if (tmpStr == argv[ARG_IDX_LEN] || errno == EINVAL || errno == ERANGE || (len != 1 && len != 2 && len != 4)) {
		printf("Invalid length: \"%s\"\n", argv[ARG_IDX_LEN]);
		usage();
		return 1;
	}

	/* right args? */
	if ((mode == 1 && argc != MAX_ARGS) || (mode == 0 && argc != MIN_ARGS)) {
		usage();
		return 1;
	}

	/* setup */
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("ERROR: open failed");
		return 1;
	}
	if (addr2range(argv[ARG_IDX_ADDR], &range, &off, len)) {
		close(fd);
		return 1;
	}

	mem = mmap(NULL, mapLen,
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
		if (len == 1) {
			val = *(uint8_t *)tmp;
			printf("0x%08X: %02X\n", range + off, val);
		}
		else if (len == 2) {
			val = *(uint16_t *)tmp;
			printf("0x%08X: %04X\n", range + off, val);
		}
		else if (len == 4) {
			val = *(uint32_t *)tmp;
			printf("0x%08X: %08X\n", range + off, val);
		}
		ret = 0;
		goto out;
	}

	if (mode == 1) {
		errno = 0;
		val = strtoul(argv[ARG_IDX_VAL], &tmpStr, 16);
		if (tmpStr == argv[ARG_IDX_VAL] || errno == EINVAL || errno == ERANGE) {
			printf("ERROR: invalid value \"%s\"\n", argv[ARG_IDX_VAL]);
			usage();
			ret = 1;
			goto out;
		}

		if (len == 1) {
			*(uint8_t *)tmp = val;
			printf("Successfully wrote 0x%02X to 0x%08X\n", val, range + off);
		}
		else if (len == 2) {
			*(uint16_t *)tmp = val;
			printf("Successfully wrote 0x%04X to 0x%08X\n", val, range + off);
		}
		else if (len == 4) {
			*(uint32_t *)tmp = val;
			printf("Successfully wrote 0x%08X to 0x%08X\n", val, range + off);
		}
		ret = 0;
		goto out;
	}

out:
	munmap(mem, mapLen);
	close(fd);
	return ret;
}
