## Token Bucket

### TCFG

Syntax

```
TCFG key rate capacity [PD seconds]
```

Config a token bucket of `key`, which has a fixed `capacity` bucket and has tokens  are added at a fixed `rate`. If `key` does not exist, a new key holding a token bucket is created. If both capacity and rate are 0, the token bucket has infinite tokens. The optional `PD` is used to set the token refill period, which is 1 seconds by default.

Return

- Simple String Reply: OK if the token bucket was configured. 

### TCONSUME

Syntax

```
TCONSUME key tokens [ NORMAL | FORCE | FLEXIBLE ]
```

Consume `tokens` from the token bucket stored at `key`. 

The option modify `TCONSUME` behavior if the token bucket has not enough `tokens`.

- NORMAL -- Return 0. The default behavior. 
- FORCE -- Return the requested tokens, which are consumed forcibly.
- FLEXIBLE -- Return the available tokens.

Return

- Integer Reply:  the number of tokens that were allow to consume, or -1 if key does not exist.

### TINFO

Syntax

```
TINFO key
```

Return information of token bucket stored at `key`.

Return

- Array reply with information of the token bucket.

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
## Fair Token Bucket

### FTCFG

Syntax

```
FTCFG key rate capacity divisor [PD seconds]
```

Config a fair token bucket of `key`, except `rate` and `capacity` as normal token bucket, it has a `divisor` which is used to set a different table size to record the deficit of each member. The specified divisor must be a power of two. The optional `PD` is used to set the token refill period, which is 1 seconds by default.
If `key` does not exist, a new key holding a fair token bucket is created. 

Return

- Simple String Reply: OK if the token bucket was configured. 

### FTCONSUME

Syntax

```
FTCONSUME key member weight tokens
```

Consume `tokens` as `member` with `weight` from the fair token bucket stored at `key`. 

The `weight` must be within the range of 1 to 20.

Return

- Integer Reply:  the number of tokens that were allow to consume, or -1 if key does not exist.

### FTINFO

Syntax

```
FTINFO key
```

Return information of the fair token bucket storead at `key`. The `ActiveMembers` is estimated by HyperLogLog.

Return

- Array reply with information of the filter.

Example

```
swarmkv-2-nodes> ftcfg abc 1000 2000 4096
OK
swarmkv-2-nodes> ftconsume abc 1 2 20
(integer) 20
swarmkv-2-nodes> ftinfo abc
 1) "Rate"
 2) (integer) 1000
 3) "Capacity"
 4) (integer) 2000
 5) "Consumed"
 6) (integer) 120
 7) "Refilled"
 8) (integer) 2120
 9) "Available"
10) (integer) 2000
11) "Divisor"
12) (integer) 4096
13) "ActiveMembers"
14) (integer) 3
```

## Bulk Token Bucket

### BTCFG

Syntax

```
BTCFG key rate capacity buckets [PD seconds]
```

Config a bulk token bucket of `key`, except `rate` and `capacity` as normal token bucket, it has a `buckets` which specifies the number of sub-token buckets. It should be far more greater than the possible members to ensure a higher accuracy. The optional `PD` is used to set the token refill period, which is 1 seconds by default.
If `key` does not exist, a new key holding a fair token bucket is created. 

Return

- Simple String Reply: OK if the token bucket was configured. 

### BTCONSUME

Syntax

```
BTCONSUME key member tokens  [ NORMAL | FORCE | FLEXIBLE ]
```

Consume `tokens` from the sub-token bucket `member` of the bulk token bucket stored at `key`. 


Return

- Integer Reply: the number of tokens that were allow to consume, or -1 if key does not exist.

### BTINFO

Syntax

```
BTINFO key [member]
```

Return information of the fair token bucket storead at `key`. The `ActiveMembers` is estimated by HyperLogLog.
You can optionally specified a `member` to query it's availalbe tokens.

Return

- Array reply with information of the filter.

Example

```
swarmkv-2-nodes> BTCFG bulk 50000 100000 2048
OK
swarmkv-2-nodes> btconsume bulk user1 2000
(integer) 2000
swarmkv-2-nodes> BTINFO bulk
 1) "Rate"
 2) (integer) 50000
 3) "Capacity"
 4) (integer) 100000
 5) "Buckets"
 6) (integer) 2048
 7) "ActiveMembers"
 8) (integer) 2
 9) "Collisions"
10) (double) 0.000000
11) "Query"
12) (integer) -1
swarmkv-2-nodes> BTINFO bulk user1
 1) "Rate"
 2) (integer) 50000
 3) "Capacity"
 4) (integer) 100000
 5) "Buckets"
 6) (integer) 2048
 7) "ActiveMembers"
 8) (integer) 2
 9) "Collisions"
10) (double) 0.000000
11) "Query"
12) (integer) 100000
```