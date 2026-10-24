/* Inert beacon for the SNMP transport host test. udp_apps.c dispatches to
 * beacon_callback(), but the real beacon walks the L2 table through the
 * ASIC registers, which this environment does not model. The transport
 * tests do not exercise the beacon, so satisfy the link with a no-op. */
#include "beacon.h"

void beacon_callback(void)
{
}
