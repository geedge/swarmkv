## Generic

### DEL

Syntax

```
DEL key
```

Removes the specified key. A key is ignored if it does not exist.

Return

- Integer reply: The number of keys that were removed.

### TYPE

Syntax

```
TYPE key
```

Returns the string representation of the type of the value stored at key. The different types that can be returned are: string, integer, set, hash, token-bucket and undefined.

Return

- String reply: type of key, or none when key does not exist.

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


### KEYSLOT

Syntax

```
KEYSLOT key
```

Returns an integer identifying the hash slot the specified key hashes to. This command is mainly useful for debugging and testing.

Return

- Integer reply: The hash slot number.
