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

- String reply: OK if SET was executed correctly.

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