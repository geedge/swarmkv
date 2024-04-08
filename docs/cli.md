# CLI

The SwarmKV command line interface (swarmkv-cli) is a terminal program used to send commands to and read replies from the SwarmKV Cluster. It has two main modes: an interactive Read Eval Print Loop (REPL) mode where the user types SwarmKV commands and receives replies, and a command mode where swarmkv-cli is executed with additional arguments and the reply is printed to the standard output.

You can start the `swarmkv-cli` by running the following command:

```
$ ./swarmkv-cli -n cluster-name
```
The `--exec` option is used to execute a command in a non-interactive fashion.

## Running the same command N times

In interactive mode, you can prefixing the command name by a number to run it N times.

```
127.0.0.1:7311@swarmkv-sync> 3 incrby id001 1
(integer) 1
(integer) 2
(integer) 3
```

Prefixing by two number are repeat times and interval (unit: second, default: 0 second, decimal is allowed, i.e., 0.1 represents 100 milliseconds).

```
127.0.0.1:7311@swarmkv-sync> 5 .1 incrby id001 1
(integer) 4
(integer) 5
(integer) 6
(integer) 7
(integer) 8
```

## Attach/Detach
Low-level commands don't have auto-route ability. If you execute low-level command via `swarmkv-cli`, you should execute `ATTACH IP:port` first. The `DETACH` command exits attaching model.

Another use case is the ability to attach to a node and execute commands on it. For example, you can connect to a node and execute the DEBUG command to debug the node.

## Trouble Shooting
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
version: 4.3.0-2-ga8acac2
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

### MONITOR
Syntax

```
MONITOR [pattern]
```

`MONITOR` is a debugging command to see all the command executed by a specific node. 
The optional `pattern` argument is a glob-style pattern that is matched against the command name and arguments. If the pattern is specified, only commands that match the pattern are streamed.
```
1711950111.894233 [1 R 127.0.0.1:43385 KRT] "get" "key"
1711950111.894324 [1 L 127.0.0.1:5212 RDR] "keyspace" "xradd" "key" "127.0.0.1:5212"
```
The abbreviations are listed before:
- `R` for remote
- `L` for local
- `KRT` for keyspace route
- `RDR` for redirect
- `FIN` for finished

It is implemented by registering the client at the attached node with the `MONREG` command, after which the attached node streams back every executed command using the `PRINT` command.

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

## Cluster Managment

### CLUSTER CREATE
Syntax

```
CLUSTER CREATE cluster-name IP:port [IP:port ...]
```
The `CLUSTER CREATE` command creates a new cluster with the specified name and adds the specified nodes to it.

### CLUSTER NODES

Syntax

```
CLUSTER NODES
```
List active nodes by communicate with Consul.

### CLUSTER KEYS

Syntax

```
CLUSTER KEYS pattern
```
Supported glob-style patterns:

- `h?llo` matches `hello`, `hallo` and `hxllo`
- `h*llo` matches `hllo` and `heeeello`
- `h[ae]llo` matches `hello` and `hallo,` but not `hillo`
- `h[^e]llo` matches `hallo`, `hbllo`, ... but not `hello`
- `h[a-b]llo` matches `hallo` and `hbllo`

Use `\` to escape special characters if you want to match them verbatim.
The pattern is exactly same as Redis https://redis.io/commands/keys/ .

### CLUSTER SANITY
Syntax

```
CLUSTER SANITY check | heal
```

The `CLUSTER SANITY` command checks for consistency between the keyspace and crdtspace. Inconsistencies are mostly caused by node crashes or restarts. The `heal` option fixes the inconsistency by adding and removing replica addresses from the keyspace.

### CLUSTER ADDSLOTOWNER
Syntax

```
CLUSTER ADDSLOTOWNER IP:Port [IP:Port ...]
```

The `CLUSTER ADDSLOTOWNER` command assigns the slots to new nodes. 


### Known Issues
If a swarmkv-cli instance attempts to connect to itself using an address other than 127.0.0.1:port, it will result in a crash.