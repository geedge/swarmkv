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