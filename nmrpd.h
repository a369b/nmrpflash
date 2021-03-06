/**
 * nmrpflash - Netgear Unbrick Utility
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
 *
 * nmrpflash is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nmrpflash is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with nmrpflash.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef NMRPD_H
#define NMRPD_H
#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32) || defined(_WIN64)
#define NMRPFLASH_WINDOWS
#elif defined(__linux__)
#define NMRPFLASH_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
#define NMRPFLASH_OSX
#elif defined(__unix__)
#define NMRPFLASH_UNIX
#warning "nmrpflash is not fully supported on your operating system"
#endif

#ifndef NMRPFLASH_WINDOWS
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#ifndef NMRPFLASH_LINUX
#include <net/if_dl.h>
#endif
#else
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

enum nmrp_op {
	NMRP_UPLOAD_FW = 0,
	NMRP_UPLOAD_ST = 1,
	NMRP_SET_REGION = 2,
};

struct nmrpd_args {
	unsigned rx_timeout;
	unsigned ul_timeout;
	const char *tftpcmd;
	const char *file_local;
	const char *file_remote;
	const char *ipaddr;
	const char *ipmask;
	const char *intf;
	const char *mac;
	enum nmrp_op op;
	uint16_t port;
	const char *region;
};

const char *leafname(const char *path);
int tftp_put(struct nmrpd_args *args);
bool tftp_is_valid_filename(const char *filename);

int nmrp_do(struct nmrpd_args *args);

int select_fd(int fd, unsigned timeout);
const char *mac_to_str(uint8_t *mac);

#ifdef NMRPFLASH_WINDOWS
void win_perror2(const char *msg, DWORD err);
void sock_perror(const char *msg);
#else
#define sock_perror(x) perror(x)
#endif

extern int verbosity;

struct ethsock;

struct ethsock *ethsock_create(const char *intf, uint16_t protocol);
int ethsock_close(struct ethsock *sock);
int ethsock_send(struct ethsock *sock, void *buf, size_t len);
ssize_t ethsock_recv(struct ethsock *sock, void *buf, size_t len);
int ethsock_set_timeout(struct ethsock *sock, unsigned msec);
uint8_t *ethsock_get_hwaddr(struct ethsock *sock);
int ethsock_arp_add(struct ethsock *sock, uint8_t *hwaddr, struct in_addr *ipaddr);
int ethsock_arp_del(struct ethsock *sock, uint8_t *hwaddr, struct in_addr *ipaddr);
int ethsock_list_all(void);

struct ethsock_ip_callback_args
{
	struct in_addr *ipaddr;
	struct in_addr *ipmask;
	void *arg;
};

typedef int (*ethsock_ip_callback_t)(struct ethsock_ip_callback_args *args);
int ethsock_for_each_ip(struct ethsock *sock, ethsock_ip_callback_t callback,
		void *arg);
#endif
