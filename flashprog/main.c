/*
 * Copyright (C) 2013 Chris McClelland
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <libfpgalink.h>
#include <liberror.h>
#include "args.h"

#define TURBO    (1<<0)
#define ENABLE   (1<<1)
#define SUPPRESS (1<<2)

static struct FLContext *handle = NULL;
static uint8 config = TURBO;

static inline FLStatus setConfig(uint8 mask, uint8 val, uint8 prevCount, const char **error) {
	uint8 buf[256], i;
	for ( i = 0; i < prevCount; i++ ) {
		buf[i] = config;
	}
	config = (uint8)(config & ~mask);
	config |= val;
	buf[i++] = config;
	return flWriteChannel(handle, 1000, 0x01, i, buf, error);
}

#define CMD_BUF1_FLASH 0x83
#define CMD_BUF1_WRITE 0x84
#define CMD_BUF1_READ  0xD1
#define CMD_STATUS     0xD7
#define CMD_READ       0x03

#define BM_READY       0x80

typedef enum {
	FLASH_SUCCESS,
	FLASH_FPGALINK,
	FLASH_FILE
} FlashStatus;

// First write a page to one of the SRAM buffers:
//   w1 07;w0 84000000;w0 "random.dat";w1 0707070707070707070707070707070705
//
// Now kick off a write of the SRAM buffer into the flash array:
//   w1 07;w0 83000000;w1 0505
//
// Now poll the status register until the top bit is cleared:
//   do {
//     w1 07;w0 D7;w1 02;w0 FF;w1 05;r0 1
//   } while ( !(statusByte & READY) );
//
FlashStatus flash(const char *fileName, uint32 pageSize, uint32 pageShift, const char **error) {
	FlashStatus retVal = FLASH_SUCCESS, status;
	uint8 tmp[pageSize+4];
	uint16 i;
	uint32 pageNum = 0;
	size_t count;
	union {
		uint32 i;
		uint8 b[4];
	} u;
	FILE *file = fopen(fileName, "rb");
	CHECK_STATUS(!file, FLASH_FILE, cleanup, "flash(): Unable to read from %s", fileName);

	printf("Flashing");
	count = fread(tmp+4, 1, pageSize, file);
	while ( count ) {
		// Write to buffer 1...
		//
		status = setConfig(
			ENABLE | SUPPRESS,  // mask
			ENABLE | SUPPRESS,  // value
			0,                  // prevCount
			error
		);
		CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
		
		tmp[0] = CMD_BUF1_WRITE;
		tmp[1] = 0x00;
		tmp[2] = 0x00;
		tmp[3] = 0x00;
		status = flWriteChannel(handle, 1000, 0x00, pageSize+4, tmp, error);
		CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
		
		status = setConfig(
			ENABLE,  // mask
			0x00,    // value
			16,      // prevCount
			error
		);
		CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
		
		// Kick off flash of buffer 1 data...
		//		
		status = setConfig(
			ENABLE,  // mask
			ENABLE,  // value
			0,       // prevCount
			error
		);
		CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
		
		u.i = pageNum << pageShift;
		tmp[0] = CMD_BUF1_FLASH;
		tmp[1] = u.b[2];  // TODO: this assumes little-endian machine
		tmp[2] = u.b[1];
		tmp[3] = u.b[0];
		status = flWriteChannel(handle, 1000, 0x00, 4, tmp, error);
		CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
		
		status = setConfig(
			ENABLE,  // mask
			0x00,    // value
			16,      // prevCount
			error
		);
		CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
		
		// Read status byte until done
		//
		i = 0;
		do {
			status = setConfig(
				ENABLE | SUPPRESS,  // mask
				ENABLE,             // value
				0,                  // prevCount
				error
			);
			CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
			
			tmp[0] = CMD_STATUS;
			tmp[1] = 0x00;
			status = flWriteChannel(handle, 1000, 0x00, 2, tmp, error);
			CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
			
			status = setConfig(
				ENABLE,  // mask
				0x00,    // value
				16,      // prevCount
				error
			);
			CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
			
			status = flReadChannel(handle, 1000, 0x00, 2, tmp, error);
			CHECK_STATUS(status, FLASH_FPGALINK, cleanup, "flash()");
			i++;
		} while ( !(tmp[1] & BM_READY) );
		printf(".");
		fflush(stdout);
		
		pageNum++;
		count = fread(tmp+4, 1, pageSize, file);
	}
	printf("\n");
cleanup:
	return retVal;
}

int main(int argc, const char *argv[]) {
	int retVal = 0;
	FLStatus status;
	FlashStatus flashStatus;
	const char *error = NULL;
	bool flag;
	bool isNeroCapable, isCommCapable;
	const char *vp = NULL, *ivp = NULL, *progConfig = NULL;
	//const char *portConfig = NULL;
	const char *flashSize = NULL;
	const char *fileName = NULL;
	const char *const prog = argv[0];
	uint32 pageSize = 0;
	uint32 pageShift = 0;

	printf("SPI-Flash Example Copyright (C) 2013 Chris McClelland\n\n");
	argv++;
	argc--;
	while ( argc ) {
		if ( argv[0][0] != '-' ) {
			unexpected(prog, *argv);
			FAIL(1, cleanup);
		}
		switch ( argv[0][1] ) {
		case 'h':
			usage(prog);
			FAIL(0, cleanup);
			break;
		case 'v':
			GET_ARG("v", vp, 2, cleanup);
			break;
		case 'i':
			GET_ARG("i", ivp, 3, cleanup);
			break;
		case 's':
			GET_ARG("s", flashSize, 5, cleanup);
			break;
		case 'p':
			GET_ARG("p", progConfig, 6, cleanup);
			break;
		case 'f':
			GET_ARG("f", fileName, 7, cleanup);
			break;
		default:
			invalid(prog, argv[0][1]);
			FAIL(8, cleanup);
		}
		argv++;
		argc--;
	}
	if ( !vp ) {
		missing(prog, "v <VID:PID>");
		FAIL(9, cleanup);
	}
	if ( !flashSize ) {
		missing(prog, "s <size:shift>");
		FAIL(10, cleanup);
	}
	pageSize = (uint32)strtoul(flashSize, (char**)&flashSize, 10);
	if ( !pageSize ) {
		fprintf(stderr, "The flash size should look like <528:10>\n");
		FAIL(11, cleanup);
	}
	if ( *flashSize != ':' ) {
		fprintf(stderr, "The flash size should look like <528:10>\n");
		FAIL(12, cleanup);
	}
	pageShift = (uint32)strtoul(flashSize+1, (char**)&flashSize, 10);
	if ( !pageShift ) {
		fprintf(stderr, "The flash size should look like <528:10>\n");
		FAIL(13, cleanup);
	}
	if ( *flashSize != '\0' ) {
		fprintf(stderr, "The flash size should look like <528:10>\n");
		FAIL(14, cleanup);
	}

	printf("pageSize = %d\npageShift = %d\n", pageSize, pageShift);

	status = flInitialise(0, &error);
	CHECK_STATUS(status, 15, cleanup);
	
	printf("Attempting to open connection to FPGALink device %s...\n", vp);
	status = flOpen(vp, &handle, NULL);
	if ( status ) {
		if ( ivp ) {
			int count = 60;
			printf("Loading firmware into %s...\n", ivp);
			status = flLoadStandardFirmware(ivp, vp, &error);
			CHECK_STATUS(status, 16, cleanup);
			
			printf("Awaiting renumeration");
			flSleep(1000);
			do {
				printf(".");
				fflush(stdout);
				status = flIsDeviceAvailable(vp, &flag, &error);
				CHECK_STATUS(status, 17, cleanup);
				flSleep(100);
				count--;
			} while ( !flag && count );
			printf("\n");
			if ( !flag ) {
				fprintf(stderr, "FPGALink device did not renumerate properly as %s\n", vp);
				FAIL(18, cleanup);
			}
			
			printf("Attempting to open connection to FPGLink device %s again...\n", vp);
			status = flOpen(vp, &handle, &error);
			CHECK_STATUS(status, 19, cleanup);
		} else {
			fprintf(stderr, "Could not open FPGALink device at %s and no initial VID:PID was supplied\n", vp);
			FAIL(20, cleanup);
		}
	}

	isNeroCapable = flIsNeroCapable(handle);
	isCommCapable = flIsCommCapable(handle);
	if ( progConfig ) {
		printf("Executing programming configuration \"%s\"...\n", progConfig);
		if ( isNeroCapable ) {
			status = flProgram(handle, progConfig, NULL, &error);
			CHECK_STATUS(status, 21, cleanup);
		} else {
			fprintf(stderr, "Program operation requested but device does not support NeroProg\n");
			FAIL(17, cleanup);
		}
	}
	status = flFifoMode(handle, 0x01, &error);
	CHECK_STATUS(status, 22, cleanup);

	if ( fileName ) {
		if ( isCommCapable ) {
			flashStatus = flash(fileName, pageSize, pageShift, &error);
			if ( flashStatus ) { FAIL(23, cleanup); }
		} else {
			fprintf(stderr, "Flash operation requested but device does not support CommFPGA\n");
			FAIL(24, cleanup);
		}
	}
cleanup:
	if ( error ) {
		fprintf(stderr, "%s\n", error);
		flFreeError(error);
	}
	flClose(handle);
	return retVal;
}

void usage(const char *prog) {
	printf("Usage: %s [-h] [-i <VID:PID>] -v <VID:PID> [-p <progConfig>]\n         -s <size:shift> -f <binFile>\n\n", prog);
	printf("Load FX2LP firmware, load the FPGA, interact with the FPGA.\n\n");
	printf("  -i <VID:PID>     initial vendor and product ID of the FPGALink device\n");
	printf("  -v <VID:PID>     renumerated vendor and product ID of the FPGALink device\n");
	printf("  -s <size:shift>  set the page size and page-address shift\n");
	printf("  -p <progConfig>  configuration and programming file\n");
	printf("  -f <flashFile>   file to load into flash\n");
	printf("  -h               print this help and exit\n");
}
