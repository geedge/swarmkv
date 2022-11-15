# Commands



## Generic

### DEL

Syntax

```
DEL key
```

Removes the specified key. A key is ignored if it does not exist.

Return

- Integer reply: The number of keys that were removed.

### EXPIRE

Syntax

```
EXPIRE key seconds
```

Set a timeout on `key`. After the timeout has expired, the key will automatically be deleted. 

Return

- Integer reply, specifically: 
  - 1 if the timeout was set.
  - 0 if the timeout was not set. e.g. key doesn't exist

### TTL

Syntax

```
TTL key
```

Returns the remaining time to live of a key that has a timeout. 

Return

- Integer reply: TTL in seconds, in case of error:  
  - -2 if the key does not exist; 
  - -1 if the key exists but has no associated expire.

### PERSIST

Syntax

```
PERSIST key
```

Remove the existing timeout on `key`, turning the key from *volatile* (a key with an expire set) to *persistent* (a key that will never expire as no timeout is associated).

Return

- Integer reply, specifically:
  - 1 if the timeout was removed.
  - 0 if key does not exist or does not have an associated timeout.

## String and Integer

### GET

Syntax

```shell
GET key
```

Get the value of `key`.

Return

- String reply: the value of key, or nil when key does not exist.
- An error is returned if the value stored at `key` is not a string or integer, because `GET`only handles string and integer values.

### SET

Syntax

```
SET key value
```

Set `key` to hold the string `value`. If `value` is a numeric string, it's consider as an integer. If `key` already holds string or integer value, it is overwritten.

Return

- Simple string reply: OK if SET was executed correctly.

### INCRBY

Syntax

```
INCRBY key increment
```

Increments the number stored at `key` by `increment`. If the key does not exist, it is set to `0` before performing the operation. An error is returned if the key contains a value of the wrong type or contains a string that can not be represented as integer. This operation is limited to 64 bit signed integers.

Return

- Integer reply: the value of key after the increment

### INCR

Syntax

```
INCR key
```

Increments the number stored at `key` by one. If the key does not exist, it is set to `0` before performing the operation. An error is returned if the key contains a value of the wrong type or contains a string that can not be represented as integer. This operation is limited to 64 bit signed integers.

Return

- Integer reply: the value of key after the increment

### DECR

Syntax

```
DECR key
```

Decrements the number stored at `key` by one. If the key does not exist, it is set to `0` before performing the operation

Return

- Integer reply: the value of key after the decrement

## Set

### SADD

Syntax

```
SADD key member [member ...]
```

Add the specified members to the set stored at `key`. Specified members that are already a member of this set are ignored. If `key` does not exist, a new set is created before adding the specified members.

An error is returned when the value stored at `key` is not a set.

Return

- Integer reply: the number of elements that were added to the set, not including all the elements already present in the set.

### SCARD

Syntax

```
SCARD key
```

Returns the set cardinality (number of elements) of the set stored at `key`.

Return

- Integer reply: the cardinality (number of elements) of the set, or `0` if `key` does not exist.

### SREM

Syntax

```
SREM key member [member ...]
```

Remove the specified members from the set stored at `key`. Specified members that are not a member of this set are ignored. If `key` does not exist, it is treated as an empty set and this command returns `0`.

An error is returned when the value stored at `key` is not a set.

Return

- Integer reply: the number of members that were removed from the set, not including non existing members. The key will NOT be deleted if all members are removed. If key does not exist, it is treated as an empty set and this command returns 0.

### SISMEMBER

Syntax

```
SISMEMBER key member
```

Returns if `member` is a member of the set stored at `key`.

Return

- Integer reply, specifically:
  - `1` if the element is a member of the set.
  - `0` if the element is not a member of the set, or if `key` does not exist.

### SMEMBERS

Syntax

```
SMEMBERS key
```

Returns all the members of the set value stored at `key`.

Return

- Array reply: all elements of the set. or empty list if the key does not exist.

## Hash

### HSET

Syntax

```
HSET key field value [field value ...]
```

Sets `field` in the hash stored at `key` to `value`. If `key` does not exist, a new key holding a hash is created. If `field` already exists in the hash, it is overwritten.

Return

- Integer reply: The number of fields that were added.

### HDEL

Syntax

```
HDEL key field [field ...]
```

Removes the specified fields from the hash stored at `key`. Specified fields that do not exist within this hash are ignored. If `key` does not exist, it is treated as an empty hash and this command returns `0`.

Return

- Integer reply: the number of fields that were removed from the hash, not including specified but non existing fields.

### HGET

Syntax

```
HGET key field
```

Returns the value associated with `field` in the hash stored at `key`.

Return

- String reply: the value associated with field, or nil when field is not present in the hash or key does not exist.

### HMGET

Syntax

```
HMGET key field [field ...]
```

Returns the values associated with the specified `fields` in the hash stored at `key`.

For every `field` that does not exist in the hash, a `nil` value is returned. Running `HMGET` against a non-existing `key` will return a a `nil` reply.

Return

- Array reply: list of values associated with the given fields, in the same order as they are requested.

### HINCRBY

Syntax

```
HINCRBY key field increment
```

Increments the number stored at `field` in the hash stored at `key` by `increment`. If `key` does not exist, a new key holding a hash is created. If `field` does not exist the value is set to `0` before the operation is performed.

The range of values supported by `HINCRBY` is limited to 64 bit signed integers.

Return

- Integer reply: the value at field after the increment operation.

### HKEYS

Syntax

```
HKEYS key
```

Returns all field names in the hash stored at `key`.

Return

- Array reply: list of fields in the hash, or an empty list when key does not exist.

### HLEN

Syntax

```
HLEN key
```

Returns the number of fields contained in the hash stored at `key`.

Return

- Integer reply: number of fields in the hash, or 0 when key does not exist.

### HGETALL

Syntax

```
HGETALL key
```

Returns all fields and values of the hash stored at `key`. In the returned value, every field name is followed by its value, so the length of the reply is twice the size of the hash.

Return

- Array reply: list of fields and their values stored in the hash, or an empty list when key does not exist.

## Token Bucket

### TCFG

Syntax

```
TCFG key capacity rate
```

Config a token bucket of `key`, which has a fixed `capacity` bucket and has tokens  are added at a fixed `rate`. If `key` does not exist, a new key holding a token  bucket is created.

Return

- Simple String Reply: OK if the token bucket was configured. 

### TCONSUME

Syntax

```
TCONSUME key tokens [ NORMAL | FORCE | FLEXIBLE ]
```

Consume `tokens` from a token from the token bucket stored at `key`. 

The option modify `TCONSUME` behavior if the token bucket has not enough `tokens`.

- NORMAL -- Return 0. The default behavior. 

- FORCE -- Return the requested tokens, which are consumed forcibly.
- FLEXIBLE -- Return the available tokens.

Return

- String Reply:  the number of tokens that were allow to consume, or -1 if key does not exist.

### TINFO

Syntax

```
TINFO key
```

Return information about `key`  token bucket.

Return

- Array reply with information of the filter.

Example

```
swarmkv-sync> tcfg tb-192.168.0.1 20000 10000
OK
swarmkv-sync> tinfo tb-192.168.0.1
 1) "Capacity"
 2) (integer) 20000
 3) "Rate"
 4) (integer) 10000
 5) "Consumed"
 6) (integer) 1080
 7) "Refilled"
 8) (integer) 21080
 9) "Available"
10) (integer) 20000
```

## Cluster Management

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

- Bulk string reply: as a collection of text lines. Lines can contain a section name (starting with a # character) or a property. All the properties are in the form of **field:value** terminated by \r\n.

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

## Cluster Management

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
| SADD                     | key member [member ...]              | Integer reply: the number of elements that were added to the set, not including all the elements already present in the set. |
| SREM                     | key member [member ...]              | Integer reply: the number of members that were removed from the set, not including non existing members. The key will NOT be deleted if all members are removed. If key does not exist, it is treated as an empty set and this command returns 0. |
| SCARD                    | key                                  | Integer reply: the cardinality (number of elements) of the set, or 0 if key does not exist. |
| SISMEMBER                | key member                           | Integer reply, specifically:<br/><br/>1 if the element is a member of the set.<br/>0 if the element is not a member of the set, or if key does not exist. |
| SMEMBERS                 | key                                  | Array reply: all elements of the set. Empty array if the key does not exist. |
| TCFG                     | key capacity rate                    | Simple String Reply: OK if the token bucket was configured. Create the token bucket if key does not exist. |
| TCONSUM                  | key tokens [NORMAL\|FORCE\|FLEXIBLE] | Integer reply： the number of tokens that were allow to consume, or -1 if key does not exist. |
| TINFO                    | key                                  | Array Reply: <br/>1) "Capacity"<br/>2) (integer) 1000000<br/>3) "Rate"<br/>4) (integer) 1000<br/>5) "Consumed"<br/>6) (integer) 5239123<br/>7) "Available"<br/>8) (integer) 3113<br/> |
| HGET                     |                                      |                                                              |
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
| CRDT PUSH                | key blob                             | Simple string reply: OK if PUSH was executed correctly.      |
| CRDT PULL                | key                                  | Blog reply: a blob of state-based CRDT                       |
| CLUSTER KEYS             | pattern                              | Array reply: list of keys matching pattern.                  |
| CLUSTER KEYSLOT          | keyslot                              | Integer reply: The hash slot the specified key hashes to.    |
| CLUSTER NODES            |                                      |                                                              |
| CLUSTER SLOTS            |                                      |                                                              |
| CLUSTER ADDNODE          | IP:port                              |                                                              |
| TUNNEL              | IP:port                              |                                                              |

NOTE: 

- Most of SwarmKV commands are identical the  [Redis Commands](https://redis.io/commands/).
- CLUSTER commands are used for maintainance and debugging purpose, and only available in *swarmkv-cli*.

