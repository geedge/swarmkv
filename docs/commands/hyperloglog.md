## HyperLogLog

[HyperLogLog](https://en.wikipedia.org/wiki/HyperLogLog) is a probabilistic data structure that estimates the cardinality of a set.  The inventor of this algorithm was Philippe Flajolet, so HyperLogLog commands are prefixed with PF.
The SwarmKV implementation is based on the [Staggered HyperLogLog (STHLL)](https://www.sciencedirect.com/science/article/abs/pii/S0140366422002407) variant, which supports near-continuous-time cardinality estimation.

### PFINIT

Syntax

```
PFINIT key precison [TIME window-milliseconds]
```

 Initialize a HyperLogLog that can estimates cardinality of elements within a sliding time window with following arguments:
 - precision -- The data of the HyperLogLog is stored in an array of m registers. m is 2^precision, e.g. precision=4, m=16. The precision should be between 4 to 16.
 - TIME -- Optional. Creates a time-limited HyperLogLog. 'window-milliseconds' specifies the duration of the time window in milliseconds. This mode allows HyperLogLog to estimate the cardinality of items within the range (now - window-milliseconds, now].

The HyperLogLog use 6 bits per register, so for precision=14, the data structure  is ~12KB (2^14*6/8 bytes). The relative error of HLL is 1.04/sqrt(m). 

Return

- Simple String Reply: OK if the bloom filter created successfully.
- Empty Array if key already exists.

Examples
```
swarmkv-2-nodes> pfinit myhll 4
OK
swarmkv-2-nodes> pfadd myhll a b c d e
(integer) 1
swarmkv-2-nodes> pfcount myhll
(integer) 4
swarmkv-2-nodes> keyspace rlist myhll
1) 127.0.0.1:5211
swarmkv-2-nodes> attach 127.0.0.1:5211
OK
127.0.0.1:5211@swarmkv-2-nodes> crdt info myhll
1) "Type"
2) "hyperloglog"
3) "Size"
4) (integer) 213
5) "Peers"
6) (integer) 0
7) "LastModified"
8) (integer) 1712380180
```

### PFADD

Syntax

```
PFADD key item [item ...]
```

Add eatch `item` to the HyperLogLog stored at `key`. 

Return

- Integer reply: 1 if at least one HyperLogLog internal register was altered.
- Integer reply: 0 if no HyperLogLog internal registers were altered.
- Integer reply: -1 if the key does not exist.

### PFCOUNT

Syntax

```
PFCOUNT key
```
Approximates the number of unique items (cardinality) observed by the HyperLogLog stored at key.
Return
- Integer reply: the approximated number of unique elements observed via PFADD.
- Integer reply: -1 if the key does not exist.

### PFINFO

Syntax

```
PFINFO key
```
Returns the precision and time window settings of the HyperLogLog stored at `key`.
Return
- Array reply with argument name (Simple string reply) and value (Integer reply or Double reply) pairs
- Empty array reply if key does not exist.

Examples
```
swarmkv-2-nodes> pfinfo myhll
1) "Error"
2) (double) 0.260000
3) "Precision"
4) (integer) 4
5) "TimeWindowMs"
6) (integer) 0
```
### Limits
Given a STHLL with m registers and a time window W, the reset interval is 2W/m. Theoretically, if k unique items are added at time 0, you would expect none (0) to be counted at time W due to resets. However, due to the staggered reset design, approximately k/2 items are observed, demonstrating a balance between maintaining sensitivity to recent data and the inherent limitations of tracking over sliding windows.