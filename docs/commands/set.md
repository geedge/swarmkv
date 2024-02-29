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

- Array reply: all elements of the set, or empty list if the key does not exist.