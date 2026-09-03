#ifndef _UDPAPPS_H_
#define _UDPAPPS_H_

#include "dhcp.h"
#include "syslog.h"
#include "beacon.h"
#ifdef WITH_SNMP
#include "snmp.h"
#endif

void udp_callbacks(void);

#ifndef UIP_UDP_APPCALL
#define UIP_UDP_APPCALL udp_callbacks
#endif /* UIP_UDP_APPCALL */

#endif
