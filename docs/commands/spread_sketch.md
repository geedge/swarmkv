## Spread Sketch

Spread Sketch is a probabilistic data structure that estimates the cardinalities of a huge number of entries. It's implemented based on [A High-Performance Invertible Sketch for Network-Wide Superspreader Detection](https://ieeexplore.ieee.org/abstract/document/9858870/) and innovated for the support of near-continuous time window based on the [Staggered HyperLogLog (STHLL)](https://www.sciencedirect.com/science/article/abs/pii/S0140366422002407) and level decay mechanism. The unique counter in sketch is implemented by STHLL instead of Multiple Resolution Bitmap suggested in the original paper.


### SSINITBYDIM

Syntax

```
SSINITBYDIM key width depth precision [TIME window-milliseconds]
```

 Initialize a Spread Sketch that can estimates cardinality of entries within a sliding time window with following arguments:
 - width: Number of counters in each array. Reduces the error size.
 - depth: Number of counter-arrays. Reduces the probability for an error of a certain size. 
 - precision -- Used by Hyperloglog, controlling the number of registers in the HyperLogLog. The precision should be between 4 to 16.
 - TIME -- Optional. Creates a time-limited Spread Sketch. 'window-milliseconds' specifies the duration of the time window in milliseconds. This mode allows estimating the cardinality of items within the range (now - window-milliseconds, now).

The error of Spread Sketch is determined by combining the error of STHLL and the error of CM Sketch. The memory usage of Spread Sketch can be estimated by width * depth * (4 + m), and gets slightly larger as more entries are added.

Return
- Simple String Reply: OK if the HyperLogLog created successfully.
- Empty Array if key already exists.


### SSINITBYCAPACITY

Syntax

```
SSINITBYCAPACITY key capacity precision [TIME window-milliseconds]
```

 Initialize a Spread Sketch that can estimates cardinality of entries within a sliding time window with following arguments:
 - capacity -- The expected number of entries which will be inserted. It determines the width and depth of the sketch by `width = capacity` an `depth = lg(capacity)`. Width less than 40 will be set to 40, and depth less than 3 will be set to 3.
 - precision -- The data of the HyperLogLog is stored in an array of m registers. m is 2^precision, e.g. precision=4, m=16. The precision should be between 4 to 16.
 - TIME -- Optional. Creates a time-limited Spread Sketch. 'window-milliseconds' specifies the duration of the time window in milliseconds. This mode allows estimating the cardinality of items within the range (now - window-milliseconds, now).

Return
- Simple String Reply: OK if the HyperLogLog created successfully.
- Empty Array if key already exists.


### SSADD

Syntax

```
SSADD key entry item [item ...]
```

Add each `entry` and `item` to the Spread Sketch stored at `key`. For each the `entry`, the cardinality of `item` will be estimated by the Spread Sketch.

Return
- Integer reply an updated cardinality of the `entry` in the sketch.
- Integer reply -1 if the key does not exist.

### SSLIST

Syntax

```
SSLIST key
```

Returns all the entries in record (usually those with high cardinality in high probability) as well as the cardinality of each entry.

Return
- Array of key-cardinality pairs if the key exists.
- Empty Array if the key does not exist.

Examples
```
swarmkv-basic-test> SSINITBYCAPACITY dos_port_scan 100 6
OK
swarmkv-basic-test> SSADD dos_port_scan 1.1.1.1 1.1.1.1:22-244.10.1.1:8888 
(integer) 1
swarmkv-basic-test> SSADD dos_port_scan 10.10.10.10 any_string_can_be_item item2 just_add_many
(integer) 3
swarmkv-basic-test> SSLIST dos_port_scan
1) "1.1.1.1"
2) (integer) 1
3) "10.10.10.10"
4) (integer) 3
```

### SSQUERY

Syntax

```
SSQUERY key entry
```

Returns the cardinality for an entry in a sketch.

Return

- Integer reply: The cardinality for an entry in a sketch.
- Integer 0 if key does not exist.


### SSMQUERY

Syntax

```
SSMQUERY key entry [entry ...]
```

Returns the cardinality for one or more entries. It's the batch query operation version of `SSQUERY`.

Return
- Array reply of Integer reply with an cardinality of each of the entries in the sketch.
- Empty Array if key does not exist.



### SSINFO

Syntax
```
SSINFO key
```
Returns the information of the sketch.
- Width: The parameter specified in `SSINITBYDIM` or if the SS is init with `SSINITBYCAPACITY`, width approximately equals to capacity and slightly larger since they are tuned by the results of experiments.
- Depth: The parameter specified in `SSINITBYDIM` or if the SS is init with `SSINITBYCAPACITY`, depth equals to lg(capacity).
- Precision: The precision of the HyperLogLog.
- TimeWindowMs: The time window in milliseconds. 0 if the SS is not time-limited.
- ErrorCMS: The error rate of the sketch. Contributes to the error in correlation with total cardinality.
- ErrorHLL: The error rate of the HyperLogLog. Contributes to the error in correlation with each entry.
- Probability: The probability of inflated cardinality.

Return
- Array reply with information of the Spread Sketch.

Examples
```
swarmkv-basic-test> SSINFO dos_port_scan
 1) "Width"
 2) (integer) 150
 3) "Depth"
 4) (integer) 3
 5) "Precision"
 6) (integer) 6
 7) "TimeWindowMs"
 8) (integer) 0
 9) "ErrorCMS"
1)  (double) 0.013333
2)  "ErrorHLL"
3)  (double) 0.130000
4)  "Probability"
5)  (double) 0.125000
```
