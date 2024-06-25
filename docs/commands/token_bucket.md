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

Return information of token bucket stored at `key`. The `LastConsumeMs` represents the unix timestamp of last consume in milliseconds.

Return

- Array reply with information of the token bucket.

Example

```
swarmkv-n-nodes> tcfg my-speed 1000 1000 1
OK
swarmkv-n-nodes> tinfo my-speed
 1) "Rate"
 2) (integer) 1000
 3) "Period"
 4) (integer) 1
 5) "Capacity"
 6) (integer) 1000
 7) "Consumed"
 8) (integer) 0
 9) "Refilled"
10) (integer) 1000
11) "Available"
12) (integer) 1000
13) "LastConsumeMs"
14) (integer) 1718613420376
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

Return information of the fair token bucket storead at `key`. The `ActiveMembers` is estimated with Staggered HyperLogLog.

Return

- Array reply with information of the filter.

Example

```
swarmkv-2-nodes> ftcfg fair-100Mbps 100000000 200000000 1024
OK
swarmkv-2-nodes> ftinfo fair-100Mbps
 1) "Refill"
 2) (integer) 100000000
 3) "Period"
 4) (integer) 1
 5) "Capacity"
 6) (integer) 200000000
 7) "Consumed"
 8) (integer) 0
 9) "Refilled"
10) (integer) 200000000
11) "Available"
12) (integer) 200000000
13) "LastConsumeMs"
14) (integer) 1718613527811
15) "Divisor"
16) (integer) 1024
17) "ActiveMembers"
18) (integer) 0
swarmkv-2-nodes>
```

## Bulk Token Bucket

### BTCFG

Syntax

```
BTCFG key rate capacity buckets [PD seconds]
```

Configure a bulk token bucket for a `key`. A bulk token bucket holds multiple token buckets of same configuration. It uses cuckoo hashing to resolve key collisions. In addition to the usual `rate` and `capacity` settings of a normal token bucket, this configuration includes a `buckets` parameter, which specifies the initial number of buckets. The value for `buckets` must be a power of two and should be proportional to the potential number of members; otherwise, it will be expanded by a factor of 2. The optional `PD` parameter sets the token refill period, which defaults to 1 second.

If `key` does not exist, a new key holding a bulk token bucket is created. 

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
BTINFO key
```

Return information of the bulk token bucket storead at `key`.

Return

- Array reply with information of the filter.

Example

```
swarmkv-2-nodes> btcfg everyone-has-20Mbps 20000000 20000000 128
OK
swarmkv-2-nodes> btconsume everyone-has-20Mbps 192.168.1.52 1000
(integer) 1000
swarmkv-2-nodes> btinfo everyone-has-20Mbps
 1) "Rate"
 2) (integer) 20000000
 3) "Period"
 4) (integer) 1
 5) "Capacity"
 6) (integer) 20000000
 7) "InitialBuckets"
 8) (integer) 128
 9) "Buckets"
10) (integer) 128
11) "ActiveBuckets"
12) (integer) 0
13) "Replicas"
14) (integer) 0
```

### BTQUERY

Syntax

```
BTQUERY key member
```

Return information of the member of the bulk token bucket storead at `key`.

Return

- Array reply with information of the filter. 

Example

```
swarmkv-2-nodes> btquery everyone-has-20Mbps 192.168.0.1
 1) "Consumed"
 2) (integer) 10000
 3) "Refilled"
 4) (integer) 20010000
 5) "Available"
 6) (integer) 20000000
 7) "Replicas"
 8) (integer) 1
 9) "LastConsumeMs"
10) (integer) 1718611846903
11) "Hash"
12) "5094063868914008036.13828113893114866413"
```