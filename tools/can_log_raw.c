/*
 * Raw CAN frame logger
 *
 * Prints all CAN frames seen by SocketCAN, appending the timestamp and the
 * CAN interface.
 * Optionally takes an argument of the file to which it writes.
 *
 *
 * Author: Alex Layton <awlayton@purdue.edu>
 *
 * Copyright (C) 2013 Purdue University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
 
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
 
// #include <linux/can.h>
// #include <linux/can/raw.h>

#include "../socketcan-isobus/patched/can.h"
#include "../socketcan-isobus/patched/raw.h"
#include "../socketcan-isobus/isobus.h"
 
void print_frame(FILE *fd, char interface[], struct timeval *ts, 
		struct isobus_mesg *mesg) {
	int i;

	/* Output CAN frame */
	fprintf(fd, "<0x%05x> [%01x]", mesg->pgn, mesg->dlen);
	for(i = 0; i < mesg->dlen; i++) {
		fprintf(fd, " %02x", mesg->data[i]);
	}
	/* Output timestamp and CAN interface */
	fprintf(fd, "\t%ld.%06ld\t%s\r\n", ts->tv_sec, ts->tv_usec, interface);

	/* Flush output after each frame */
	fflush(fd);
}

int main(int argc, char *argv[]) {
	int s;
	struct sockaddr_can addr;
	struct iovec iov;
	struct ifreq ifr;
	FILE *fo;

	char ctrlmsg[CMSG_SPACE(sizeof(struct timeval))+CMSG_SPACE(sizeof(__u32))];
	struct isobus_mesg mesg;


	if((s = socket(PF_CAN, SOCK_DGRAM, CAN_ISOBUS)) < 0) {
		fprintf(stdout, "socket failure\n");
		return EXIT_FAILURE;
	}

	/* Listen on all CAN interfaces */
	addr.can_family  = AF_CAN;
	addr.can_ifindex = 0;
	addr.can_addr.isobus.addr = ISOBUS_ANY_ADDR;

	if(bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stdout, "bind failure\n");
		return EXIT_FAILURE;
	}

	/* Timestamp frames */
	const int val = 1;
	setsockopt(s, SOL_CAN_ISOBUS, CAN_ISOBUS_DADDR, &val, sizeof(val));
	setsockopt(s, SOL_SOCKET, SO_TIMESTAMP, &val, sizeof(val));

	/* Log to first argument as a file */
	if(argc > 1) {
		fo = fopen(argv[1], "w");
	} else {
		fo = stdout;
	}

	/* Buffer received CAN frames */
	struct msghdr msg = { 0 };

	/* Construct msghdr to use to recevie messages from socket */
	msg.msg_name = &addr;
	msg.msg_namelen = sizeof(addr);
	msg.msg_iov = &iov;
	msg.msg_control = ctrlmsg;
	msg.msg_controllen = sizeof(ctrlmsg);
	msg.msg_iovlen = 1;
	iov.iov_base = &mesg;
	iov.iov_len = sizeof(mesg);
	while(1) {
		/* Print received CAN frames */
		if(recvmsg(s, &msg, 0) <= 0) {
			perror("recvmsg");
			continue;
		}

		/* Find approximate receive time */
		struct cmsghdr *cmsg;
		struct sockaddr_can daddr = { 0 };
		struct timeval tv = { 0 };
		for(cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
				cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			if(cmsg->cmsg_level == SOL_CAN_ISOBUS &&
				cmsg->cmsg_type == CAN_ISOBUS_DADDR) {
			memcpy(&daddr, CMSG_DATA(cmsg), sizeof(daddr));
			}
			if(cmsg->cmsg_level == SOL_SOCKET &&
					cmsg->cmsg_type == SO_TIMESTAMP) {
				memcpy(&tv, CMSG_DATA(cmsg), sizeof(tv));
				break;
			}
		}

		/* Find name of receive interface */
		ifr.ifr_ifindex = addr.can_ifindex;
		ioctl(s, SIOCGIFNAME, &ifr);

		/* Print fames to STDOUT */
		print_frame(fo, ifr.ifr_name, &tv, &mesg);
	}

	return EXIT_SUCCESS;
}

