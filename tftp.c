/**
 * nmrp-flash - Netgear Unbrick Utility
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
 *
 * nmrp-flash is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nmrp-flash is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with nmrp-flash.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _BSD_SOURCE
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include "nmrpd.h"

#define TFTP_PKT_SIZE 516

static const char *opcode_names[] = { 
	"RRQ", "WRQ", "DATA", "ACK", "ERR"
};

enum tftp_opcode {
	RRQ  = 1,
	WRQ  = 2,
	DATA = 3,
	ACK  = 4,
	ERR  = 5
};

static inline void pkt_mknum(char *pkt, uint16_t n)
{
	*(uint16_t*)pkt = htons(n);
}

static inline uint16_t pkt_num(char *pkt)
{
	return ntohs(*(uint16_t*)pkt);
}

static void pkt_mkwrq(char *pkt, const char *filename, const char *mode)
{
	size_t len = 2;

	pkt_mknum(pkt, WRQ);

	strcpy(pkt + len, filename);
	len += strlen(filename) + 1;
	strcpy(pkt + len, mode);
	len += strlen(mode) + 1;
}

static inline void pkt_print(char *pkt, FILE *fp)
{
	uint16_t opcode = pkt_num(pkt);
	if (!opcode || opcode > ERR) {
		fprintf(fp, "(%d)", opcode);
	} else {
		fprintf(fp, "%s", opcode_names[opcode - 1]);
		if (opcode == ACK || opcode == DATA) {
			fprintf(fp, "(%d)", pkt_num(pkt + 2));
		} else if (opcode == WRQ) {
			fprintf(fp, "(%s, %s)", pkt + 2, pkt + 2 + strlen(pkt + 2) + 1);
		}
	}
}

static ssize_t tftp_recvfrom(int sock, char *pkt, struct sockaddr_in *src)
{
	socklen_t socklen;
	ssize_t len;

	(void)src, (void)socklen;

	len = recvfrom(sock, pkt, TFTP_PKT_SIZE, 0, NULL, NULL);
	if (len < 0) {
		if (errno != EAGAIN) {
			perror("recvfrom");
			return -1;
		}

		return -2;
	}

	uint16_t opcode = pkt_num(pkt);

	if (opcode == ERR) {
		fprintf(stderr, "Error (%d): %.511s\n", pkt_num(pkt + 2), pkt + 4);
		return -1;
	} else if (!opcode || opcode > ERR) {
		/* The EX2700 I've tested this on sends a raw TFTP packet with no
		 * opcode, and an error message starting at offset 0.
		 */
		fprintf(stderr, "Error: %.32s\n", pkt);
		return -3;
	}

	return len;
}

static ssize_t tftp_sendto(int sock, char *pkt, size_t len, 
		struct sockaddr_in *dst)
{
	ssize_t sent;

	switch (pkt_num(pkt)) {
		case RRQ:
		case WRQ:
			len = 2 + strlen(pkt + 2) + 1;
			len += strlen(pkt + len) + 1;
			break;
		case DATA:
			len += 4;
			break;
		case ACK:
			len = 4;
			break;
		case ERR:
			len = 4 + strlen(pkt + 4);
			break;
		default:
			fprintf(stderr, "Error: Invalid packet ");
			pkt_print(pkt, stderr);
			return -1;
	}

	sent = sendto(sock, pkt, len, 0, (struct sockaddr*)dst, sizeof(*dst));
	if (sent < 0) {
		perror("sendto");
	}

	return sent;
}

int sock_set_rx_timeout(int fd, unsigned msec)
{
	struct timeval tv;

	if (msec) {
		tv.tv_usec = (msec % 1000) * 1000;
		tv.tv_sec = msec / 1000;
		if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
			perror("setsockopt(SO_RCVTIMEO)");
			return 1;
		}
	}

	return 0;
}

int tftp_put(struct nmrpd_args *args)
{
	struct sockaddr_in addr;
	uint16_t block;
	ssize_t len;
	int fd, sock, err, timeout, last_len;
	char rx[TFTP_PKT_SIZE], tx[TFTP_PKT_SIZE];

	fd = open(args->filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		err = fd;
		goto cleanup;
	}

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		perror("socket");
		err = sock;
		goto cleanup;
	}

	err = sock_set_rx_timeout(sock, args->rx_timeout);
	if (err) {
		goto cleanup;
	}

	err = !inet_aton(args->ipaddr, &addr.sin_addr);
	if (err) {
		perror("inet_aton");
		goto cleanup;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(args->port);

	pkt_mkwrq(tx, args->filename, "octet");

	len = tftp_sendto(sock, tx, 0, &addr);
	if (len < 0) {
		err = len;
		goto cleanup;
	}

	len = tftp_recvfrom(sock, rx, &addr);
	if (len < 0) {
		err = len;
		goto cleanup;
	}

	timeout = 0;
	block = 0;
	last_len = -1;

	do {
		if (timeout || (pkt_num(rx) == ACK && pkt_num(rx + 2) == block)) {
			if (!timeout) {
				++block;
				pkt_mknum(tx, DATA);
				pkt_mknum(tx + 2, block);
				len = read(fd, tx + 4, 512);
				if (len < 0) {
					perror("read");
					err = len;
					goto cleanup;
				} else if (!len) {
					if (last_len != 512) {
						break;
					}
				}

				last_len = len;
			}

			err = tftp_sendto(sock, tx, len, &addr);
			if (err < 0) {
				goto cleanup;
			}
		} else if (pkt_num(rx) != ACK) {
			fprintf(stderr, "Expected ACK(%d), got ", block);
			pkt_print(rx, stderr);
			fprintf(stderr, "!\n");
		}

		err = tftp_recvfrom(sock, rx, &addr);
		if (err < 0) {
			if (err == -2) {
				if (++timeout < 5) {
					continue;
				}
				fprintf(stderr, "Timeout while waiting for ACK(%d)\n.", block);
			}
			goto cleanup;
		} else {
			timeout = 0;
			err = 0;
		}
	} while(1);

	err = 0;

cleanup:
	if (fd >= 0) {
		close(fd);
	}

	if (sock >= 0) {
		close(sock);
	}

	return err;
}