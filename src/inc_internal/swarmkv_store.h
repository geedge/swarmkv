#pragma once

#include "swarmkv_limits.h"
#include "swarmkv_keyspace.h"
#include "uthash.h"
#include "utarray.h"

#include <pthread.h>

enum swarmkv_data_type
{
	STRING,
	INTEGER,
	HASH,
	THROTTLE,
	__SWARMKV_DATA_TYPE_MAX
};
struct swarmkv_store;

