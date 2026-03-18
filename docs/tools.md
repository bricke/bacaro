# Bacaro — Tools

Bacaro ships two optional CLI tools useful for development, testing, and debugging. Both are built by default and can be disabled with `BACARO_BUILD_TOOLS=OFF`.

## oste (bacaro-monitor)

`oste` subscribes to all properties on the bus and prints every update as it arrives, one line per property:

```
[14:32:01.042] sensors.cpu.temperature          = 71.3  (from: powerd)
[14:32:01.043] network.eth0.rx_bytes            = 1048576  (from: netd)
[14:32:01.105] system.status                    = "running"  (from: init)
```

Run it directly or via its installed alias:

```sh
oste
# or
bacaro-monitor
```

`oste` does not publish anything. It exits cleanly on `Ctrl+C`.

The name comes from Venetian dialect — the *oste* is the host of a bacaro, watching everything that happens at the bar.

## vecio (bacaro-set)

`vecio` sets a single property from the command line and exits immediately. Useful for injecting test values while `oste` is running alongside.

```sh
vecio <path> <value>
# or
bacaro-set <path> <value>
```

Value type is inferred automatically:

| Input | Packed as |
|-------|-----------|
| `true` / `false` | bool |
| Integer (e.g. `42`) | int64 |
| Decimal (e.g. `3.14`) | float64 |
| Anything else | string |

Examples:

```sh
vecio sensors.cpu.temperature 71.3
vecio system.status running
vecio system.reboot_count 5
vecio system.maintenance true
```

The name comes from Venetian dialect — *vecio* means "old man", a regular at the bar who always has something to say.
