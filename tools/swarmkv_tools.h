#pragma once
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct cluster_manager_command
{
	char* cluster_name;
	int argc;
	char ** argv;
};
struct config
{	
	char db_name[256];
	unsigned int consul_port;
	unsigned int cluster_port;
	char consul_host[64];
	struct swarmkv *db;
	struct cluster_manager_command cluster_mgr_cmd;
};
struct config g_config;

struct swarmkv_reply *cluster_sanity_command(struct swarmkv *db, char *argv[], size_t argc);
struct swarmkv_reply *cluster_keys_command(struct swarmkv *db, char *argv[], size_t argc);
struct swarmkv_reply *cluster_addslotowner_command(struct swarmkv *db, char *argv[], size_t argc);
struct swarmkv_reply *cluster_slots_command(struct swarmkv *db, char *argv[], size_t argc);
struct swarmkv_reply *cluster_create_command(struct swarmkv *db, char *argv[], size_t argc);
struct swarmkv_reply *cluster_nodes_command(struct swarmkv *db, char *argv[], size_t argc);

#ifdef __cplusplus
} /* end extern "C" */
#endif