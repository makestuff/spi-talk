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
#include "args.h"

// Header stuff
#define SD_SUCCESS              0
#define SD_READBLOCK_CMD_ERROR  1
#define SD_INIT_NOT_IDLE_ERROR  2
#define SD_INIT_TIMEOUT_ERROR   3

#define LOG2_BYTES_PER_SECTOR   9
#define BYTES_PER_SECTOR        (1<<LOG2_BYTES_PER_SECTOR)

// Body stuff
#define TOKEN_SUCCESS             0x00
#define TOKEN_READ_CAPACITY       0xFE
#define TOKEN_READ_SINGLE         0xFE
#define TOKEN_READ_MULTIPLE       0xFE
#define TOKEN_WRITE_SINGLE        0xFE
#define TOKEN_WRITE_MULTIPLE      0xFC
#define TOKEN_WRITE_FINISH        0xFD
#define IN_IDLE_STATE             (1<<0)
#define ERASE_RESET               (1<<1)
#define ILLEGAL_COMMAND           (1<<2)
#define COMMAND_CRC_ERROR         (1<<3)
#define ERASE_SEQUENCE_ERROR      (1<<4)
#define ADDRESS_ERROR             (1<<5)
#define PARAMETER_ERROR           (1<<6)

#define CMD_GO_IDLE_STATE         0
#define CMD_SEND_OP_COND          1
#define CMD_SEND_CSD              9
#define CMD_STOP_TRANSMISSION     12
#define CMD_READ_SINGLE_BLOCK     17
#define CMD_READ_MULTIPLE_BLOCKS  18
#define CMD_WRITE_SINGLE_BLOCK    24
#define CMD_WRITE_MULTIPLE_BLOCKS 25
#define CMD_APP_SEND_OP_COND      41
#define CMD_APP_CMD               55

#define SD_RETCODE_SUCCESS             0x00
#define SD_RETCODE_NOT_IDLE            1
#define SD_RETCODE_ERROR_INIT          2
#define SD_RETCODE_ERROR_READ          3
#define SD_RETCODE_ERROR_NO_DATA       4
#define SD_RETCODE_ERROR_READ_CAPACITY 5
#define SD_RETCODE_ERROR_WRITE         6
#define SD_RETCODE_ERROR_NOT_READY     7

static struct FLContext *handle = NULL;
static uint8 config = 0x00;

#define TURBO  (1<<0)
#define ENABLE (1<<1)

static inline void enable(void) {
	config |= ENABLE;
	FLStatus status = flWriteChannel(handle, 1000, 0x01, 1, &config, NULL);
}

static inline void disable(void) {
	config &= ~ENABLE;
	FLStatus status = flWriteChannel(handle, 1000, 0x01, 1, &config, NULL);
}

static inline void fast(void) {
	config |= TURBO;
	FLStatus status = flWriteChannel(handle, 1000, 0x01, 1, &config, NULL);
}

static inline void slow(void) {
	config &= ~TURBO;
	FLStatus status = flWriteChannel(handle, 1000, 0x01, 1, &config, NULL);
}

static inline uint8 spiSendByte(uint8 byte) {
	FLStatus status;
	// TODO: actually handle errors here
	status = flWriteChannel(handle, 1000, 0x00, 1, &byte, NULL);
	status = flReadChannel(handle, 1000, 0x00, 1, &byte, NULL);
	return byte;
}

static inline FLStatus spiSendBlock(uint8 *ioBlock) {
	FLStatus status;
	status = flWriteChannel(handle, 1000, 0x00, 512, ioBlock, NULL);
	if ( status ) { return status; }
	status = flReadChannel(handle, 1000, 0x00, 512, ioBlock, NULL);
	return status;
}

static inline uint8 waitForNot(uint8 response) {
	uint16 i = 0xFFFF;
	uint8 byte;
	do {
		byte = spiSendByte(0xFF);
	} while ( byte == response && --i );
	return byte;
}

static inline uint8 waitFor(uint8 response) {
	uint16 i = 0xFFFF;
	uint8 byte;
	do {
		byte = spiSendByte(0xFF);
	} while ( byte != response && --i );
	return byte;
}

static void sendClocks(uint8 numClocks, uint8 byte) {
	uint8 i;
	for ( i = 0; i < numClocks; i++ ) {
		spiSendByte(byte);
	}
}

static uint8 sendCommand(uint8 command, uint32 param) {
	spiSendByte(0xFF);      // dummy byte
	spiSendByte(command | 0x40);
	spiSendByte(param>>24);
	spiSendByte(param>>16);
	spiSendByte(param>>8);
	spiSendByte(param);
	spiSendByte(0x95);  // correct CRC for first command in SPI
	                    // after that CRC is ignored, so no problem with
	                    // always sending 0x95
	spiSendByte(0xFF);  // ignore return byte
	return waitForNot(0xFF);
}

uint8 sdInit(void) {
	uint16 i;
	uint8 returnCode;
	
	// Setup SPI @ 400kHz, deassert CS
	//
	disable();
	slow();
	
	// Enable SPI; set as master; select Fosc/64 (250kHz). For initialization,
	// the clock has to be between 100kHz and 400kHz
	//
	//SPCR = (1<<MSTR) | (1<<SPE) | (3<<SPR0);
	//SPSR = (1<<SPI2X);
	
	// Hold CS high and send 256 clocks (1.024ms @250kHz) with DI low while card power stabilizes
	//
	sendClocks(32, 0x00);
	
	// Hold CS high and send 80 clocks (0.32ms @250kHz) with DI high to get the card ready to accept commands
	//
	sendClocks(10, 0xFF);
	
	// Bring CS low and send CMD0 to reset and put the card in SPI mode
	//
	enable();
	if ( sendCommand(CMD_GO_IDLE_STATE, 0) != IN_IDLE_STATE ) {
		sendClocks(2, 0xFF);
		disable();
		return SD_INIT_NOT_IDLE_ERROR;
	}
	
	// Tell the card to initialize itself with ACMD41.
	//
	sendCommand(CMD_APP_CMD, 0);
	returnCode = sendCommand(CMD_APP_SEND_OP_COND, 0);
	
	// Send CMD1 repeatedly until the initialization is complete.
	//
	i = 0xFFFF;
	while ( returnCode != TOKEN_SUCCESS && --i ) {
		returnCode = sendCommand(CMD_SEND_OP_COND, 0);
	}
	
	sendClocks(2, 0xFF);
	disable();
	
	if ( i == 0x0000 ) {
		return SD_INIT_TIMEOUT_ERROR;
	}

	// Enable 24MHz SPI mode
	//fast();
	return SD_SUCCESS;
}

static struct SdStatus {
	int reserved      : 16 - LOG2_BYTES_PER_SECTOR - 1;
	int isMultiple    : 1;
	int currentOffset : LOG2_BYTES_PER_SECTOR;
} status = {0, 0, 0};

uint8 sdGetByte(void) {
	uint8 byte;
	if ( !status.currentOffset ) {
		waitFor(status.isMultiple ? TOKEN_READ_MULTIPLE : TOKEN_READ_SINGLE);
	}
	byte = spiSendByte(0xff);
	status.currentOffset++;
	if ( !status.currentOffset ) {
		sendClocks(2, 0xFF);  // Flush two CRC bytes
	}
	return byte;
}

uint8 sdReadSingleBlockBegin(uint32 lba) {
	uint16 timeout;
	uint8 returnCode;

	enable();
	// Send read single block command and Logical Block Address
	//
	timeout = 0xFFFF;
	while ( (returnCode = sendCommand(CMD_READ_SINGLE_BLOCK, lba<<9)) != TOKEN_SUCCESS && --timeout );
	if ( !timeout ) {
		sendClocks(2, 0xFF);
		disable();
		printf(
			"sdReadSingleBlockBegin() encountered SD_READBLOCK_CMD_ERROR {\n  lba=0x%08X\n  returnCode=0x%02X\n}\n",
			lba, returnCode
		);
		return SD_READBLOCK_CMD_ERROR;
	}
	status.currentOffset = 0;
	status.isMultiple = 0;
	return SD_SUCCESS;
}

uint8 sdReadMultipleBlocksBegin(uint32 lba) {
	uint16 timeout;

	enable();
	// Send read multiple blocks command and Logical Block Address
	//
	timeout = 0xFFFF;
	while ( sendCommand(CMD_READ_MULTIPLE_BLOCKS, lba<<9) != TOKEN_SUCCESS && --timeout );
	if ( timeout == 0x0000 ) {
		sendClocks(2, 0xFF);
		disable();
		printf("sdReadMultipleBlocksBegin() encountered SD_READBLOCK_CMD_ERROR!\n");
		return SD_READBLOCK_CMD_ERROR;
	}
	status.currentOffset = 0;
	status.isMultiple = 1;
	return SD_SUCCESS;
}

void sdSkip(uint16 numBytes) {
	while ( numBytes-- ) {
		sdGetByte();
	}
}

uint16 sdGetWord(void) {
	return (uint16)sdGetByte() + ((uint16)sdGetByte() << 8);
}

uint32 sdGetLong(void) {
	return (uint32)sdGetByte() + ((uint32)sdGetByte() << 8) + ((uint32)sdGetByte() << 16) + ((uint32)sdGetByte() << 24);
}

void sdGetBytes(uint8 *buffer, uint16 numBytes) {
	while ( numBytes-- ) {
		*buffer++ = sdGetByte();
	}
}

void sdReadBlocksEnd(void) {
	if ( status.currentOffset ) {
		sdSkip(BYTES_PER_SECTOR - status.currentOffset);
	}
	if ( status.isMultiple ) {
		uint16 timeout = 0xFFFF;
		while ( sendCommand(CMD_STOP_TRANSMISSION, 0) != TOKEN_SUCCESS && --timeout );
	}
	disable();
}

void sdTest(uint32 blkNum) {
	uint16 i;
	uint8 block[512];
	printf("Reading SD card block 0x%08X...\n", blkNum);
	sdReadSingleBlockBegin(blkNum);
	waitFor(TOKEN_READ_SINGLE);
	spiSendBlock(block);
	sdReadBlocksEnd();
	for ( i = 0; i < 512; i++ ) {
		printf("%c", block[i]);
	}
	sendClocks(2, 0xFF);  // Flush two CRC bytes
	printf("\n");
}

// Main stuff
#define CHECK(x) if ( status != FL_SUCCESS ) { FAIL(x); }

int main(int argc, const char *argv[]) {
	int returnCode = 0;
	FLStatus status;
	const char *error = NULL;
	bool flag;
	bool isNeroCapable, isCommCapable;
	bool spiFast = false;
	const char *vp = NULL, *ivp = NULL, *portConfig = NULL, *progConfig = NULL;
	const char *blockStr = NULL;
	uint32 blockNum = 0x00002672;
	const char *const prog = argv[0];

	printf("SD-Card Hackery Example Copyright (C) 2013 Chris McClelland\n\n");
	argv++;
	argc--;
	while ( argc ) {
		if ( argv[0][0] != '-' ) {
			unexpected(prog, *argv);
			FAIL(1);
		}
		switch ( argv[0][1] ) {
		case 'h':
			usage(prog);
			FAIL(0);
			break;
		case 'v':
			GET_ARG("v", vp, 2);
			break;
		case 'i':
			GET_ARG("i", ivp, 3);
			break;
		case 'd':
			GET_ARG("d", portConfig, 4);
			break;
		case 'p':
			GET_ARG("p", progConfig, 5);
			break;
		case 'f':
			spiFast = true;
			break;
		case 'b':
			GET_ARG("b", blockStr, 6);
			break;
		default:
			invalid(prog, argv[0][1]);
			FAIL(7);
		}
		argv++;
		argc--;
	}
	if ( !vp ) {
		missing(prog, "v <VID:PID>");
		FAIL(8);
	}

	status = flInitialise(0, &error);
	CHECK(9);
	
	printf("Attempting to open connection to FPGALink device %s...\n", vp);
	status = flOpen(vp, &handle, NULL);
	if ( status ) {
		if ( ivp ) {
			int count = 60;
			printf("Loading firmware into %s...\n", ivp);
			status = flLoadStandardFirmware(ivp, vp, &error);
			CHECK(10);
			
			printf("Awaiting renumeration");
			flSleep(1000);
			do {
				printf(".");
				fflush(stdout);
				status = flIsDeviceAvailable(vp, &flag, &error);
				CHECK(11);
				flSleep(100);
				count--;
			} while ( !flag && count );
			printf("\n");
			if ( !flag ) {
				fprintf(stderr, "FPGALink device did not renumerate properly as %s\n", vp);
				FAIL(12);
			}
			
			printf("Attempting to open connection to FPGLink device %s again...\n", vp);
			status = flOpen(vp, &handle, &error);
			CHECK(13);
		} else {
			fprintf(stderr, "Could not open FPGALink device at %s and no initial VID:PID was supplied\n", vp);
			FAIL(14);
		}
	}

	if ( blockStr ) {
		blockNum = strtoul(blockStr, NULL, 0);
	}

	if ( portConfig ) {
		printf("Configuring ports...\n");
		status = flPortConfig(handle, portConfig, &error);
		CHECK(15);
		flSleep(100);
	}

	isNeroCapable = flIsNeroCapable(handle);
	isCommCapable = flIsCommCapable(handle);
	if ( progConfig ) {
		printf("Executing programming configuration \"%s\"...\n", progConfig);
		if ( isNeroCapable ) {
			status = flProgram(handle, progConfig, NULL, &error);
			CHECK(16);
		} else {
			fprintf(stderr, "Program operation requested but device does not support NeroProg\n");
			FAIL(17);
		}
	}
	status = flFifoMode(handle, true, &error);
	CHECK(18);

	sdInit();
	if ( spiFast ) {
		fast();
	}
	sdTest(blockNum);
	
cleanup:
	if ( error ) {
		fprintf(stderr, "%s\n", error);
		flFreeError(error);
	}
	flClose(handle);
	return returnCode;
}

void usage(const char *prog) {
	printf("Usage: %s [-h] [-i <VID:PID>] -v <VID:PID> [-p <progConfig>]\n\n", prog);
	printf("Load FX2LP firmware, load the FPGA, interact with the FPGA.\n\n");
	printf("  -i <VID:PID>    initial vendor and product ID of the FPGALink device\n");
	printf("  -v <VID:PID>    renumerated vendor and product ID of the FPGALink device\n");
	printf("  -d <portConfig> configure the ports\n");
	printf("  -p <progConfig> configuration and programming file\n");
	printf("  -f              enable fast SPI\n");
	printf("  -b <block>      block to read from SD card\n");
	printf("  -h              print this help and exit\n");
}
