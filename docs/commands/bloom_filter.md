## Bloom Filter

### BFRESERVE

Syntax

```
BFRESERVE key error_rate capacity [TIME window-milliseconds slice-number]
```

 Create an empty bloom filter that can keep elements within a sliding time window with following arguments:
 - error_rate -- Probability of collision (as long as the capacity is not exceeded). Should be between 0.000001 to 0.1.
 - capacity -- The expected number of element which will be inserted. At least 1000. If the actual number of elements exceeds the capacity, the bloom filter will expand.
 - TIME -- Create a time-limited bloom filter. The following arguments are required:
   - window-milliseconds -- The duration of the time window in milliseconds. Items in time range (now - window-milliseconds, now] are kept.
   - slice-number -- The number of time slices within the window, which sets the granularity of timeouts to window-milliseconds/slice-number. The value should be between 0 and 64.
- COUNT --- Create a count-limited bloom filter. **NOT IMPLEMENTED YET**

Return

- Simple String Reply: OK if the bloom filter created successfully.
- Empty Array if key already exists.

### BFADD

Syntax

```
BFADD key item [item ...]
```

Add every `item` to the bloom filter stored at `key`. 


Return

- Simple String Reply: OK if the items are added to the bloom filter successfully.

### BFEXISTS

Syntax

```
BFEXISTS key item
```

Determines if the `item` was added to the bloom filter stored at `key`. This command is similar to BFMEXISTS, except that only one item can be checked.

Return

- Integer reply - where "1" means that, with high probability, item was already added to the filter, and "0" means that key does not exist or that item was definitely not added to the filter.

### BFMEXISTS

Syntax

```
BFMEXISTS key item [item ...]
```

Determines if each `item` was added to the bloom filter stored at `key`. 


Return

- Array reply of Integer reply - where "1" means that, with high probability, item was already added to the filter, and "0" means that key does not exist or that item was definitely not added to the filter.

### BFCARD

Syntax

```
BFCARD key
```
Approximating the number of items (cardinality) of the Bloom filter stored at `key`.

Return

- Integer Reply: the estimated number of items, or 0 if the key does not exist.

### BFINFO

Syntax

```
BFINFO key
```
Returns information about a Bloom filter.
- ErrorRate -- error_rate arguments specified in BFRESERVE.
- Capacity -- capacity arguments specified in BFRESERVE.
- TimeWindowMs -- window-milliseconds arguments specified in BFRESERVE.
- TimeSlices -- slice-number arguments specified in BFRESERVE.
- HashNum -- Hash function number caculated by the error_rate, which is log2(1/error_rate).
- TotalSlices -- The actual slice number after expansion.
- MaxExpansionTimes -- The maximum expansion times of every time slice.
- ApproximateItemNum -- The estimated number of items in the filter. Same as value as `BFCARD` returned.
- FillRatio -- The maximum fill ratio of every slice. Fill ratio that higher than 0.5 indicates high false positives.
- OldestItemTime -- The possible oldest item of the filter.

Return

- Array reply with argument name (Simple string reply) and value (Integer reply or Double reply) pairs
- Empty array reply if key does not exist.