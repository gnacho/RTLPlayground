/*
 * beacon.c - NetPulse UDP beacon for RTLPlayground switches
 *
 * Sends one JSON datagram every BEACON_INTERVAL seconds to a NetPulse
 * server (spec v1):
 *   {"v":1,"seq":N,"slug":"switch16","token":"...","ports":[{"n":1,"l":3,"tx":123,"rx":45},...]}
 * Link codes l match status.json (0=down, else speed code + 1).
 * tx/rx are the accumulated good packet counters per port (decimal).
 *
 * Configured via CLI/config/web command:
 *   beacon                 - status
 *   beacon off             - stop sending
 *   beacon <ip> <token>    - target server and agent token, start sending
 *
 * Internal RAM on these devices is nearly exhausted: no function
 * parameters, no locals, all working state in xdata, literals appended
 * through the already linked strtox() (shared overlay slots) and the
 * decimal conversion done by repeated subtraction (the sdcc 32 bit
 * division helpers need parameter blocks in internal RAM).
 */

#include "machine.h"
#include "beacon.h"
#include "uip/uip.h"
#include "rtl837x_sfr.h"
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_port.h"

#pragma codeseg BANK2
#pragma constseg BANK2

extern __code struct machine machine;
extern __xdata uint8_t sfr_data[4];

__xdata struct beacon_state beacon_state;
static uip_ipaddr_t __xdata beacon_ip;

#define BEACON_P ((__xdata uint8_t *)uip_appdata)

/* Working state, kept out of internal RAM */
static __xdata uint8_t * __xdata bptr;
static __xdata uint32_t bval;
static __xdata uint8_t b_n;
static __xdata uint8_t b_i;
static __xdata uint8_t b_counter;
static __xdata uint8_t b_b;
static __xdata uint8_t b_p;
static __xdata uint8_t b_c;
static __xdata uint8_t b_lead;
static __xdata uint16_t b_entry;
static __xdata uint16_t b_first_entry;
static __xdata uint8_t b_valid;
static __xdata uint8_t b_first_flag;
static __xdata uint8_t b_port;

static __code char b_hexchars[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

#define B_STR(s)	do { bptr += strtox(bptr, (s)); } while (0)

/* two hex digits of b_b */
static void b_hex2(void)
{
	*bptr++ = b_hexchars[b_b >> 4];
	*bptr++ = b_hexchars[b_b & 0xf];
}

static __code uint32_t b_pow10[10] = {
	1000000000, 100000000, 10000000, 1000000, 100000,
	10000, 1000, 100, 10, 1
};

static void b_u32(void)
{
	b_p = 0;
	b_lead = 0;
	do {
		b_c = 0;
		while (bval >= b_pow10[b_p]) {
			bval -= b_pow10[b_p];
			b_c++;
		}
		if (b_c || b_lead || b_p == 9) {
			*bptr++ = '0' + b_c;
			b_lead = 1;
		}
		b_p++;
	} while (b_p < 10);
}

void beacon_init(void) __banked
{
	beacon_state.enabled = 0;
	beacon_state.fdb_enabled = 0;
	beacon_state.conn = 0;
	beacon_state.pending = 0;
	beacon_state.seconds = BEACON_INTERVAL;
	beacon_state.fdb_pending = 0;
	beacon_state.fdb_seconds = BEACON_FDB_INTERVAL;
	beacon_state.seq = 0;
	beacon_state.token[0] = 0;
	beacon_state.server_ip[0] = 0;
	beacon_state.server_ip[1] = 0;
	beacon_state.server_ip[2] = 0;
	beacon_state.server_ip[3] = 0;
}

void beacon_start(void) __banked
{
	if (beacon_state.token[0] == 0) {
		print_string_newline_no_syslog("Beacon: no token configured");
		return;
	}
	if (beacon_state.conn == 0) {
		uip_ipaddr(beacon_ip, beacon_state.server_ip[0], beacon_state.server_ip[1],
			   beacon_state.server_ip[2], beacon_state.server_ip[3]);
		beacon_state.conn = uip_udp_new(&beacon_ip, HTONS(BEACON_PORT));
		if (beacon_state.conn == 0) {
			print_string_newline_no_syslog("Beacon: failed to create UDP connection");
			return;
		}
		beacon_state.seconds = BEACON_INTERVAL;
		beacon_state.enabled = 1;
		print_string_newline_no_syslog("Beacon: sending to IP ");
		itoa(beacon_state.server_ip[0]); write_char('.');
		itoa(beacon_state.server_ip[1]); write_char('.');
		itoa(beacon_state.server_ip[2]); write_char('.');
		itoa(beacon_state.server_ip[3]);
		write_char('\n');
	} else {
		print_string_newline_no_syslog("Beacon is already running");
	}
}

void beacon_stop(void) __banked
{
	beacon_state.enabled = 0;
	if (beacon_state.conn != 0) {
		uip_udp_remove(beacon_state.conn);
		beacon_state.conn = 0;
		print_string_newline_no_syslog("Stopped beacon");
	} else {
		print_string_newline_no_syslog("Beacon is not running");
	}
}

/* Called once per second from the main loop */
void beacon_tick(void) __banked
{
	if (!beacon_state.enabled)
		return;
	if (!beacon_state.fdb_enabled) {
		beacon_state.fdb_pending = 0;
		beacon_state.fdb_seconds = BEACON_FDB_INTERVAL;
	}
	if (beacon_state.seconds == 0) {
		beacon_state.pending = 1;
		beacon_state.seconds = BEACON_INTERVAL;
	} else {
		beacon_state.seconds--;
	}
	if (beacon_state.fdb_seconds == 0) {
		beacon_state.fdb_pending = 1;
		beacon_state.fdb_seconds = BEACON_FDB_INTERVAL;
	} else {
		beacon_state.fdb_seconds--;
	}
}

/* Link code for port b_i into bval, same computation as status.json */
static void beacon_link_code(void)
{
	reg_read_m(RTL837X_REG_LINKS_STS);
	if (!((sfr_data[(b_i / 8) + 1] >> (b_i % 8)) & 1)) {
		bval = 0;	/* link down */
		return;
	}
	if (b_i < 8)
		reg_read_m(RTL837X_REG_LINKS);
	else
		reg_read_m(RTL837X_REG_LINKS_89);
	b_b = sfr_data[3 - ((b_i & 7) >> 1)];
	b_b = ((b_i & 1) ? b_b >> 4 : b_b & 0xf) + 1;
	bval = b_b;
}

/* 32 bit MIB counter for port b_i / counter b_counter into bval,
 * assembled byte by byte with compound operations to avoid 32 bit
 * temporaries in internal RAM. */
static void beacon_stat(void)
{
	STAT_GET(b_counter, b_i);
	reg_read_m(RTL837X_STAT_V_HIGH);
	bval = sfr_data[0];
	bval <<= 8;
	bval |= sfr_data[1];
	bval <<= 8;
	reg_read_m(RTL837X_STAT_V_LOW);
	bval |= sfr_data[0];
	bval <<= 8;
	bval |= sfr_data[1];
}

/* One FDB datagram: {"v":1,"seq":N,"slug":..,"token":..,"fdb":{"MAC":"port",...}}
 * Walks the L2 table the same way send_l2() in page_impl.c does.
 * Port numbers are 1-based (matching what the scraper published). */
static void beacon_send_fdb(void)
{
	beacon_state.fdb_pending = 0;

	bptr = BEACON_P;
	B_STR("{\"v\":1,\"seq\":");
	beacon_state.seq++;
	bval = beacon_state.seq;
	b_u32();
	B_STR(",\"slug\":\"switch16\",\"token\":\"");
	b_n = 0;
	while (beacon_state.token[b_n]) {
		*bptr++ = beacon_state.token[b_n];
		b_n++;
	}
	B_STR("\",\"fdb\":{");

	b_entry = 0;
	b_first_entry = 0xffff;
	b_first_flag = 1;
	do {
		reg_read_m(RTL837X_TBL_CTRL);
	} while (sfr_data[3] & TBL_EXECUTE);

	while (1) {
		reg_read_m(RTL837x_TBL_DATA_0);
		REG_WRITE(RTL837x_TBL_DATA_0, sfr_data[0], sfr_data[1] & 0xfc,
			  sfr_data[2] | (TBL_LUTREAD_NEXT_L2UC << 6), sfr_data[3]);
		REG_WRITE(RTL837X_TBL_CTRL, b_entry >> 8, b_entry, TBL_L2_UNICAST, TBL_EXECUTE);
		do {
			reg_read_m(RTL837X_TBL_CTRL);
		} while (sfr_data[3] & TBL_EXECUTE);

		reg_read_m(RTL837x_L2_DATA_OUT_B);
		b_valid = (sfr_data[0] & 0x20) != 0;
		if (b_valid) {
			/* stop well below the buffer end */
			if (bptr > (__xdata uint8_t *)uip_appdata + 1800)
				break;
			if (b_first_flag) {
				b_first_flag = 0;
			} else {
				*bptr++ = ',';
			}
			b_port = (sfr_data[0] >> 6) & 0x3;
			*bptr++ = '"';
			/* MAC bytes 0 and 1 */
			b_b = sfr_data[2];
			b_hex2();
			b_b = sfr_data[3];
			b_hex2();
			reg_read_m(RTL837x_L2_DATA_OUT_A);
			/* MAC bytes 2 to 5 */
			b_b = sfr_data[0]; b_hex2();
			b_b = sfr_data[1]; b_hex2();
			b_b = sfr_data[2]; b_hex2();
			b_b = sfr_data[3]; b_hex2();
			*bptr++ = '"';
			*bptr++ = ':';
			*bptr++ = '"';
			reg_read_m(RTL837x_L2_DATA_OUT_C);
			b_port |= (sfr_data[3] & 0x3) << 2;
			bval = b_port + 1;
			b_u32();
			*bptr++ = '"';
		}

		reg_read_m(RTL837x_TBL_DATA_0);
		b_entry = (((uint16_t)sfr_data[2] & 0x0f) << 8) | sfr_data[3];
		b_entry += 1;

		if (b_first_entry == 0xffff)
			b_first_entry = b_entry;
		else if (b_first_entry == b_entry)
			break;
	}

	B_STR("}}");

	uip_udp_send(bptr - BEACON_P);
}

/* Called from uIP when our UDP connection is polled */
void beacon_callback(void) __banked
{
	if (beacon_state.conn == 0 || uip_udp_conn->lport != beacon_state.conn->lport)
		return;
	if (!beacon_state.enabled)
		return;
	if (beacon_state.fdb_pending && beacon_state.fdb_enabled) {
		beacon_send_fdb();
		return;
	}
	beacon_state.fdb_pending = 0;
	if (!beacon_state.pending)
		return;
	beacon_state.pending = 0;

	bptr = BEACON_P;
	B_STR("{\"v\":1,\"seq\":");
	beacon_state.seq++;
	bval = beacon_state.seq;
	b_u32();
	B_STR(",\"slug\":\"switch16\",\"token\":\"");
	b_n = 0;
	while (beacon_state.token[b_n]) {
		*bptr++ = beacon_state.token[b_n];
		b_n++;
	}
	B_STR("\",\"ports\":[");

	for (b_i = machine.min_port; b_i <= machine.max_port; b_i++) {
		if (b_i != machine.min_port)
			*bptr++ = ',';
		B_STR("{\"n\":");
		bval = b_i + 1;
		b_u32();
		B_STR(",\"l\":");
		beacon_link_code();
		b_u32();
		B_STR(",\"tx\":");
		b_counter = STAT_COUNTER_TX_PKTS;
		beacon_stat();
		b_u32();
		B_STR(",\"rx\":");
		b_counter = STAT_COUNTER_RX_PKTS;
		beacon_stat();
		b_u32();
		*bptr++ = '}';
	}

	B_STR("]}");

	uip_udp_send(bptr - BEACON_P);
}
