# Networking (userspace)

> **Last verified:** 2026-07-26  
> **State:** Partial

Modes: `NETWORK_MODE=none|static|dhcp` via `/etc/ir0/network.conf` or
`/etc/network/interfaces`. No hardcoded `eth0` or QEMU DNS in minimal.

DHCP is **blocked-by-kernel-ABI** until primitives exist. Loopback is best-effort
when `ip`/`ifconfig` applets are present and supported.
