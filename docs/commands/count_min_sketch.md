## Count-min Sketch

Count-Min Sketch is a probabilistic data structure that can be used to estimate the frequency of events/elements in a stream of data.
The CMS is not the best data structure to count frequency of a uniformly distributed stream. 

### CMSINITBYDIM

Syntax

```
CMSINITBYDIM key width depth
```
Initializes a Count-Min Sketch to dimensions specified by user.

Parameters:
- key: The name of the sketch.
- width: Number of counters in each array. Reduces the error size.
- depth: Number of counter-arrays. Reduces the probability for an error of a certain size (percentage of total count).

Return

- Simple String Reply: OK if executed correctly.
- Empty Array if key already exists or wrong parameters.

### CMSINITBYPROB

Syntax

```
CMSINITBYPROB key error probability
```
Initializes a Count-Min Sketch to accommodate requested tolerances. The error parameter will determine the width w of your sketch and the probability will determine the number of hash functions (depth d). The error rate we choose will determine the threshold above which we can trust the result from the sketch. 

Parameters:
- key: The name of the sketch.
- error: Estimate size of error. The error is a percent of **total counted items**, but not the count of single item. This determines the width of the sketch.
- probability: The desired probability for inflated count. This should be a decimal value between 0 and 1. This effects the depth of the sketch. The closer this number is to zero, the greater the memory consumption per item and the more CPU usage per operation.

Return

- Simple String Reply: OK if executed correctly.
- Empty Array if key already exists or wrong parameters.

Examples
Assume you select an error rate of 0.1% (0.001) with a certainty of 99.8% (0.998). This means you have an error probability of 0.02% (0.002). 
```
swarmkv-2-nodes> cmsinitbyprob concurrent-sessions 0.001 0.002
OK
swarmkv-2-nodes> cmsinfo concurrent-sessions
 1) "Width"
 2) (integer) 2000
 3) "Depth"
 4) (integer) 9
 5) "Error"
 6) (double) 0.001000
 7) "Probability"
 8) (double) 0.001953
 9) "Count"
10) (integer) 0
11) "ReplicaNumber"
12) (integer) 0
swarmkv-2-nodes>
```

### CMSINCRBY

Syntax

```
CMSINCRBY key item increment [item increment ...]
```

Increases the count of item by increment. Multiple items can be increased with one call.
Parameters:
- key: The name of the sketch.
- item: The item which counter is to be increased. Negtive value is allowed for decrement.
- increment: Amount by which the item counter is to be increased.

Return
- Array reply of Integer reply with an updated min-count of each of the items in the sketch.
- Empty Array if key does not exist.

### CMSQUERY

Syntax
```
CMSQUERY key item
```
Returns the count for an item in a sketch.

Parameters:
- key: The name of the sketch.
- item: The item for which to return the count.

Return
- Integer reply: The count of the item in the sketch.
- Integer 0 if key does not exist.

### CMSMQUERY

Syntax
```
CMSMQUERY key item [item ...]
```
Returns the count for one or more items in a sketch. It's the bulk version of `CMSQUERY`.

Parameters:
- key: The name of the sketch.
- item: One or more items for which to return the count.

Return
- Array reply of Integer reply with a min-count of each of the items in the sketch.
- Empty Array if key does not exist.

### CMSRLIST

Syntax
```
CMSRLIST key
```
Returns the list of replica uuid.

Parameters:
- key: The name of the sketch.

Return
- Array reply with the list of replica uuid.
- Empty Array if key does not exist.

### CMSRCLEAR

Syntax
```
CMSRCLEAR key uuid
```
Zeroes the replica identified by the given uuid. This command is intend to reset count when node holding this replica is down.

Return
- Simple String Reply: OK if executed correctly.
- Erorr reply if the replica specified by uuid is not found.

### CMSINFO

Syntax
```
CMSINFO key
```
Returns the information of the sketch.
- Width: If the CMS is init with `CMSINITBYPROB`, width = 2/error.
- Depth: If the CMS is init with `CMSINITBYPROB`, depth = -log2(probability).
- Probability: The probability of inflated count.
- Error: The error rate of the sketch.
- Count: The total count of the sketch. The inflated threshold is Count*Error.
- ReplicaNumber: The number of replicas of the sketch.

Parameters:
- key: The name of the sketch.

Return
- Array reply with information of the sketch.

Examples
```
swarmkv-2-nodes> cmsinfo cms-key
 1) "Width"
 2) (integer) 8192
 3) "Depth"
 4) (integer) 8
 5) "Error"
 6) (double) 0.000244
 7) "Probability"
 8) (double) 0.003906
 9) "Count"
10) (integer) 0
11) "ReplicaNumber"
12) (integer) 0
```