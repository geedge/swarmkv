## CLI

The SwarmKV command line interface (swarmkv-cli) is a terminal program used to send commands to and read replies from the SwarmKV Cluster. It has two main modes: an interactive Read Eval Print Loop (REPL) mode where the user types SwarmKV commands and receives replies, and a command mode where swarmkv-cli is executed with additional arguments and the reply is printed to the standard output.

### Running the same command N times

In interactive mode, you can prefixing the command name by a number to run it N times.

```
127.0.0.1:7311@swarmkv-sync> 3 incrby id001 1
(integer) 1
(integer) 2
(integer) 3
```

Prefixing by two number are repeat times and interval (unit: second).

```
127.0.0.1:7311@swarmkv-sync> 5 .1 incrby id001 1
(integer) 4
(integer) 5
(integer) 6
(integer) 7
(integer) 8
```

