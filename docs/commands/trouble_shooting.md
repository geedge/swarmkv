## Debug

### INFO

Syntax

```
INFO [section]
```

The `INFO` command returns information and statistics about the node in a format that is simple to parse by computers and easy to read by humans.

The optional parameter can be used to select a specific section of information:

- Node
- Store
- Keyspace
- Network

Return

- Bulk string reply: as a collection of text lines. Lines can contain a section name (starting with a # character) or a property. All the properties are in the form of **field: value** terminated by \r\n.

Example

```
swarmkv-sync> info
# Node
swarmkv_version: 3.0.0
address: 127.0.0.1:40619
uuid: 922eab5d-2634-47f6-934a-1504379a12a3
worker_threads: 1
server_time_usec: 1668444667564448
up_time_in_seconds: 302
up_time_in_days: 0

# Store
shards: 8
keys: 0
to_sync: 0
synced: 0
sync_ok: 0
sync_err: 0
sync_interval_in_usec:500

# Keyspace
health_check_port: 0
slots: 0
keys: 0
expires: 0

# Network
timeout_in_msec: 500.00
connections: 2
pending_rpcs: 0
timed_out_rpcs: 0
input_bytes: 4197
output_bytes: 3481
input_cmds: 0
output_cmds: 45
input_replies: 45
output_replies: 0
input_msgs: 45
output_msgs: 45
input_buffer: 333
output_buffer: 0
unknown_sequence: 0
instantaneous_input_kbps: 0.00
instantaneous_output_kbps: 0.00
instantaneous_input_cps: 0.00
instantaneous_output_cps: 0.00
```

### LATENCY

Syntax

```
LATENCY <subcommand> [<arg> [value] [opt] ...]
```

The `LANTENCY` command returns latency metrics of command execution.
Subcommands are:
* COMMAND [command]
    - Return time-latency samples for a specified command name.
* PEER [IP:port]
    - Return time-latency samples for the specified peer.
* EVENT [event]
    - Return time-latency samples for the specified event.
* RESET [command|event|peer]
    - Reset data of a specified catalog or all the data if no catalog provided.


### DEBUG

Syntax

```
DEBUG <subcommand> [<arg> [value] [opt] ...]
```

Subcommands are:
* SLEEP <seconds>
    - Stop the server for <seconds>. Decimals allowed.
* ASSERT
    - Crash by assertion failed.