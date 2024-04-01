## CLI

The SwarmKV command line interface (swarmkv-cli) is a terminal program used to send commands to and read replies from the SwarmKV Cluster. It has two main modes: an interactive Read Eval Print Loop (REPL) mode where the user types SwarmKV commands and receives replies, and a command mode where swarmkv-cli is executed with additional arguments and the reply is printed to the standard output.

You can start the `swarmkv-cli` by running the following command:

```
$ ./swarmkv-cli -n cluster-name
```
The `--exec` option is used to execute a command in a non-interactive fashion.

### Running the same command N times

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

### Attach/Detach
Low-level commands don't have auto-route ability. If you execute low-level command via `swarmkv-cli`, you should execute `ATTACH IP:port` first. The `DETACH` command exits attaching model.

Another use case is the ability to attach to a node and execute commands on it. For example, you can connect to a node and execute the DEBUG command to debug the node.

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

### MONITOR
Syntax

```
MONITOR [pattern]
```

`MONITOR` is a debugging command to see all the command executed by a specific node. It is implemented by registering the client at the attached node with the `MONREG` command, after which the attached node streams back every executed command using the `PRINT` command.

The optional `pattern` argument is a glob-style pattern that is matched against the command name and arguments. If the pattern is specified, only commands that match the pattern are streamed.
```
1711950111.894233 [1 R 127.0.0.1:43385 KRT] "get" "key" "value"
1711950111.894324 [1 L 127.0.0.1:5212 RDR] "keyspace" "xradd" "key" "127.0.0.1:5212"
```
The abbreviations are listed before:
- `R` for remote
- `L` for local
- `KRT` for keyspace route
- `RDR` for redirect
- `FIN` for finished

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