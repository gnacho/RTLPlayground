# NetPulse UDP beacon

The firmware can push switch state to a [NetPulse](https://github.com/gnacho/netpulse)
server (or any compatible collector) as small JSON datagrams over UDP. No
polling, no scraping: the switch reports on its own clock.

Reference receiver: NetPulse's UDP beacon listener
(`server-go/internal/httpapi/beacon.go`), which feeds the same device registry
as the HTTP agents.

## Transport

- UDP to the configured server IP, port **5140**.
- One JSON object per datagram, a single ASCII line (~400 B typical).
- Two datagram types: **status** (every 30 s) and **FDB** (every 300 s).

## Status datagram (v1)

```json
{"v":1,"seq":42,"slug":"kp-9000","token":"...","ports":[{"n":1,"l":3,"tx":123,"rx":45}]}
```

| field | meaning |
|---|---|
| `v`   | protocol version, always `1` |
| `seq` | per-boot sequence, incremented on every datagram (status and FDB share the counter). Receivers should tolerate reordering and not treat replays as errors |
| `slug` | device identity: the switch hostname (`hostname` config). Choose a unique one per unit |
| `token` | shared secret configured with `beacon <ip> <token>`. Sent in cleartext (LAN trust model); the receiver validates it against its own token store and answers nothing |
| `ports` | one entry per port |
| `n`   | port number, 1-based |
| `l`   | link code: `0` = down, `1` = 10M, `2` = 100M, `3` = 1G, `4` = 500M, `5` = 10G, `6` = 2.5G, `7` = 5G |
| `tx` / `rx` | accumulated good-frame counters (low 32 bits of the 64-bit MIB counters) |

## FDB datagram (v1)

```json
{"v":1,"seq":43,"slug":"kp-9000","token":"...","fdb":{"AA:BB:CC:DD:EE:FF":"port5","66:77:88:99:AA:BB":"port8"}}
```

- Sent every 300 s while enabled.
- `fdb` maps each learned unicast L2 MAC (uppercase) to the 1-based port
  number where it was learned.
- The table can be large on busy networks: the datagram is a snapshot of the
  L2 table at send time, so a missing MAC only means "not learned right now".

## Configuration (serial console or CLI)

```
beacon                       show status
beacon off                   stop sending
beacon <ip> <token>          set server and token, start sending
beacon fdb on|off            toggle the FDB datagram at runtime
```

`beacon <ip> <token>` enables the FDB datagram by default (the saved config
line carries no extra options). Everything persists through the normal config
save (`save` command or the web UI Save).

## Security model

The token travels in cleartext inside the LAN, by design: this is the same
threat model as the switch web UI and the NetPulse agent tokens. Exposure of
the token lets an impostor impersonate the switch to the collector, nothing
more. Rate limiting and sequence logging live on the receiver side.
