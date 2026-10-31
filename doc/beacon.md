# NetPulse UDP beacon

The NetPulse integration branch can push switch state to a
[NetPulse](https://github.com/gnacho/netpulse) server as JSON datagrams over
UDP. The switch sends port state and counters every 30 seconds, plus an
optional forwarding database snapshot every 300 seconds.

This code is maintained in the `gnacho/RTLPlayground` fork. It is not part of
the upstream RTLPlayground firmware.

## Before building

Installing RTLPlayground replaces the original switch firmware. Check the
[supported device list](supported_devices.md), select the exact machine
profile, and keep a backup of the original flash. The project strongly
recommends having a SOIC clamp and a working restore procedure before
installation. A failed web upgrade may require direct flash access.

The NetPulse branch currently keeps the device `slug` in `beacon.c`. Change it
to a unique NetPulse device name before building. The same name must own the
agent token on the NetPulse server.

The following tag is a reproducible base with byte counters, LLDP
transmission, the configuration save fixes, and optional SNMP support:

```sh
git clone https://github.com/gnacho/RTLPlayground.git
cd RTLPlayground
git checkout v0.1.0-netpulse-snmp2
make MACHINE=<supported-machine-profile>
```

The firmware image is written below `output/`. For the first upgrade from the
original OEM web interface, build the separate installer image:

```sh
make -C installer
```

Use `installer/output/rtlplayground_oem_upgrade.bin` only with the OEM upgrade
page. When RTLPlayground is already installed, use the normal machine-specific
image from `output/`. Follow the flashing and recovery instructions in the
[main README](../README.md).

## NetPulse pairing

Enable the UDP beacon listener in NetPulse and create an external agent with
the same device name used in `beacon.c`. Copy the generated token to the
switch. The
[NetPulse onboarding guide](https://github.com/gnacho/netpulse/blob/main/docs/RTL-SWITCH-BEACON.md)
covers the server side.

Configure the beacon from the authenticated web console or serial console:

```text
beacon 192.0.2.10 <agent-token>
beacon
beacon fdb
```

Replace the documentation address with the NetPulse server address. Setting
the server and token starts status datagrams and enables FDB datagrams. Use
`beacon off` to stop the beacon and `beacon fdb off` to disable only FDB
updates.

For LLDP transmission without flooding link-local frames to every port, review
[the RMA documentation](rma.md) and use:

```text
lldp on
rma 0e drop
```

Save the resulting startup configuration through the normal web interface.
The current NetPulse firmware line includes the LLDP configuration save fix.
Check `beacon` and `beacon fdb` after a reboot to verify the restored state.

## Transport

- Destination: the configured server IP, UDP port 5140.
- Payload: one ASCII JSON object per datagram.
- Status interval: 30 seconds.
- FDB interval: 300 seconds.
- Sequence: one counter shared by status and FDB datagrams.

The sender has no real-time clock. The receiver timestamps each datagram on
arrival.

## Status datagram

This example shows the fields in tag `v0.1.0-netpulse-snmp2`:

```json
{"v":1,"seq":42,"slug":"edge-switch","token":"...","ports":[{"n":1,"l":3,"tx":123,"rx":45,"ih":0,"il":987654,"oh":0,"ol":123456}],"up":3600,"mac":"AABBCCDDEEFF","fw":"v0.1.0-example"}
```

| Field | Meaning |
|---|---|
| `v` | Protocol version, currently `1`. |
| `seq` | Sequence number shared by status and FDB datagrams. Receivers should tolerate reordering. |
| `slug` | Device identity compiled into `beacon.c`. It must match the NetPulse agent name. |
| `token` | Shared secret configured with `beacon <ip> <token>`. |
| `ports` | One entry per front-panel port. |
| `n` | One-based port number. |
| `l` | Link code from the table below. |
| `tx`, `rx` | Low 32 bits of the accumulated good-frame counters. |
| `ih`, `il` | High and low 32-bit halves of the native 64-bit `ifInOctets` counter. |
| `oh`, `ol` | High and low 32-bit halves of the native 64-bit `ifOutOctets` counter. |
| `up` | Seconds since boot. |
| `mac` | Management MAC address as 12 uppercase hexadecimal characters. |
| `fw` | Firmware version string. |

Link codes are `0` for down, `1` for 10M, `2` for 100M, `3` for 1G, `4` for
500M, `5` for 10G, `6` for 2.5G, and `7` for 5G.

The current integration branch also sends `ie` and `oe` in each port entry.
These are the accumulated receive and transmit error counters, equivalent to
`ifInErrors` and `ifOutErrors`. They were added after tag
`v0.1.0-netpulse-snmp2`.

Older receivers can ignore unknown fields. Receivers without octet support can
fall back to frame counters, but they cannot derive bit rates from frames.

## FDB datagram

```json
{"v":1,"seq":43,"slug":"edge-switch","token":"...","fdb":{"AABBCCDDEEFF":"5","66778899AABB":"8"}}
```

Each key is a learned unicast MAC address as 12 uppercase hexadecimal
characters. Each value is the one-based port number as a string. A separate
datagram carries the table, so status updates do not grow with the number of
clients.

The FDB payload is bounded by the available UDP buffer. On a large table it
can contain only part of the learned entries. A missing MAC does not prove
that the client has disappeared.

## Security

The token travels in cleartext inside unsigned UDP datagrams. Use this beacon
only on a trusted management LAN and keep the NetPulse listener away from
untrusted networks. Anyone with the token and network access can impersonate
the switch to the collector.

Tokens can contain up to 64 characters. The receiver applies source rate
limiting and logs suspicious sequence changes.

## Optional SNMP

The tagged firmware can include the read-only SNMPv2c agent with
`make MACHINE=<supported-machine-profile> SNMP=1`. It starts disabled and
requires a community and an allowed manager address. See
[the SNMP guide](snmp.md).

NetPulse currently uses the beacon as the device source. It cannot add SNMP as
a second source for the same beacon-fed device yet. The firmware SNMP agent
can still serve an independent monitoring client.

## Limits

The beacon does not report CPU, RAM, or system temperature. LLDP transmission
helps neighboring devices discover the switch, but the beacon does not carry
an LLDP neighbor table. Port labels stay on the NetPulse side because the
firmware omits them to save datagram space.
