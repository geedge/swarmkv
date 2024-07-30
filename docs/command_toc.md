# Commands

## Command Categories

The supported command are category as follows:
* [Generic Type](./commands/generic.md)
* [String and Integer Types](./commands/string_and_integer.md)
* [Set Type](./commands/set.md)
* [Hash Type](./commands/hash.md)
* [Token Bucket Types](./commands/token_bucket.md)
* [Bloom Filter Type](./commands/bloom_filter.md)
* [Count-Min Sketch Type](./commands/count_min_sketch.md)
* [HyperLogLog Type](./commands/hyperloglog.md)
* [Spread Sketch Type](./commands/spread_sketch.md)
* [Cluster Management](./commands/cluster.md)
* [Trouble Shooting](./commands/trouble_shooting.md)
## COMMAND LIST

Syntax

```
COMMAND LIST
```

The `COMMAND LIST` command returns an array of the server's command names.

Return

- Array reply: a list of command names.



## Quick List

| Command                  | Arguments                            | Response                                                     |
| ------------------------ | ------------------------------------ | ------------------------------------------------------------ |
| GET                      | key                                  | Bulk string reply: the value of key, or nil when key does not exist. |
| SET                      | key value                            | Simple string reply: OK if SET was executed correctly.       |
| DEL                      | key                                  | Integer reply: The number of keys that were removed.         |
| INCRYBY                  | key increment                        | Integer reply: the value of key after the increment          |
| EXPIRE                   | key seconds                          | Integer reply, specifically: 1 if the timeout was set. 0 if the timeout was not set. e.g. key doesn't exist |
| TTL                      | key                                  | Integer reply: TTL in seconds, in case of error:   -2 if the key does not exist;  -1 if the key exists but has no associated expire. |
| PERSIST                  | key                                  | Integer reply, specifically:<br/><br/>1 if the timeout was removed.<br/>0 if key does not exist or does not have an associated timeout. |
| KEYSLOT                  | key                                  | Integer reply: The hash slot the specified key hashes to.    |
| SADD                     | key member [member ...]              | Integer reply: the number of elements that were added to the set, not including all the elements already present in the set. |
| SREM                     | key member [member ...]              | Integer reply: the number of members that were removed from the set, not including non existing members. The key will NOT be deleted if all members are removed. If key does not exist, it is treated as an empty set and this command returns 0. |
| SCARD                    | key                                  | Integer reply: the cardinality (number of elements) of the set, or 0 if key does not exist. |
| SISMEMBER                | key member                           | Integer reply, specifically:<br/><br/>1 if the element is a member of the set.<br/>0 if the element is not a member of the set, or if key does not exist. |
| SMEMBERS                 | key                                  | Array reply: all elements of the set. Empty array if the key does not exist. |
| HSET                     | HSET key field value [field value ...] | |
| HGET                     |
| TCFG                     | key capacity rate                    | Simple String Reply: OK if the token bucket was configured. Create the token bucket if key does not exist. |
| TCONSUME                 | key tokens [NORMAL\|FORCE\|FLEXIBLE] | Integer reply： the number of tokens that were allow to consume, or -1 if key does not exist. |
| TINFO                    | key                                  | Array Reply |
| FTCFG                    | key capacity rate divisor            | Simple String Reply: OK if the token bucket was configured. Create the token bucket if key does not exist. |
| FTCONSUME                 | key member weight tokens | Integer reply： the number of tokens that were allow to consume, or -1 if key does not exist. |
| FTINFO                    | key                                  | Array Reply|
| BTCFG                    | key capacity rate buckets            | Simple String Reply: OK if the token bucket was configured. Create the token bucket if key does not exist. |
| BTCONSUME                 | key member tokens [NORMAL\|FORCE\|FLEXIBLE] | Integer reply： the number of tokens that were allow to consume, or -1 if key does not exist. |
| BTINFO                    | key                                  | Array Reply|                                   |                                                              |
| KEYSPACE RADD            | key IP port                          | Add replica to the key, create the key if key does not exist. |
| KEYSPACE XRADD           | key IP Port                          | Add replica to the key, return NULL if the key does not exist. |
| KEYSPACE RLIST           | key                                  | Node Array reply:<br/>1) "192.168.1.200:5211"<br/>2) "192.168.1.201:5211"<br/>3) "[2001:db8::1]:5211"<br/> |
| KEYSPACE DEL             | key                                  |                                                              |
| KEYSPACE SETSLOT         | see source code                      |                                                              |
| KEYSPACE GETKEYSINSLOT   | IP port slot                         |                                                              |
| KEYSPACE ADDKEYSTOSLOT   | IP port slot blob                    |                                                              |
| KEYSPACE DELSLOTKEYS     | IP port slot                         |                                                              |
| KEYSPACE KEYS            | IP port pattern                      |                                                              |
| KEYSPACE COUNTKEYSINSLOT | slot                                 |                                                              |
| CRDT DEL                 | key                                  | Integer reply: The number of cached keys that were removed.  |
| CRDT MERGE                | key blob                            | Simple string reply: OK if MERGE was executed correctly.      |
| CRDT GET                | key                                   | Blog reply: a blob of state-based CRDT                       |
| CRDT INFO                | key                                   | Array reply:                        |
| CLUSTER KEYS             | pattern                              | Array reply: list of keys matching pattern.                  |
| CLUSTER NODES            |                                      |                                                              |
| CLUSTER SLOTS            |                                      |                                                              |
| CLUSTER ADDNODE          | IP:port                              |                                                              |
| TUNNEL              | IP:port                              |                                                              |

NOTE: 

- Most of SwarmKV commands are identical the  [Redis Commands](https://redis.io/commands/).
- CLUSTER commands are used for maintainance and debugging purpose, and only available in *swarmkv-cli*.

