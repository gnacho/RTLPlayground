#ifndef _BEACON_H_
#define _BEACON_H_

#include <stdint.h>

#define BEACON_PORT		5140
#define BEACON_INTERVAL		30	/* seconds between status datagrams */
#define BEACON_FDB_INTERVAL	300	/* seconds between FDB datagrams */
#define BEACON_TOKEN_MAX	65	/* 64 chars + NUL */

struct beacon_state {
	uint8_t enabled;
	uint8_t fdb_enabled;	/* FDB datagrams are off by default: the
				 * first field test hung the switch, to be
				 * debugged (2026-08-27). Enable with
				 * "beacon fdb on" at your own risk. */
	uint8_t server_ip[4];
	uint8_t seconds;	/* countdown to next send */
	uint8_t pending;	/* 1 = datagram queued for next uIP poll */
	uint16_t fdb_seconds;	/* countdown to next FDB send */
	uint8_t fdb_pending;	/* 1 = FDB datagram queued */
	uint16_t seq;
	char token[BEACON_TOKEN_MAX];
	struct uip_udp_conn *conn;
};

extern __xdata struct beacon_state beacon_state;

void beacon_init(void) __banked;
void beacon_start(void) __banked;
void beacon_stop(void) __banked;
void beacon_tick(void) __banked;
void beacon_callback(void) __banked;

#endif
