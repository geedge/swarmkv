#include "swarmkv_cmd_spec.h"
#include "swarmkv_utils.h"
#include "uthash.h"

#include <ctype.h> //isspace

struct swarmkv_command_table
{
    struct swarmkv_module module;
    struct swarmkv_cmd_spec *hash_table;
};
static struct swarmkv_command_table *module2command_table(const struct swarmkv_module *module)
{
	assert(0 == strcmp(module->name, "command_table"));
	struct swarmkv_command_table *table = container_of(module, struct swarmkv_command_table, module);
	assert(table == module->mod_ctx);
	return table;
}
struct swarmkv_module * swarmkv_command_table_new(void)
{
    struct swarmkv_command_table *table=ALLOC(struct swarmkv_command_table, 1);
    strncpy(table->module.name, "command_table", sizeof(table->module.name));
    table->module.mod_ctx=table;
    table->hash_table=NULL;
    return &table->module;
}
void swarmkv_command_table_free(struct swarmkv_module *mod_table)
{
    struct swarmkv_command_table *table = module2command_table(mod_table);
	struct swarmkv_cmd_spec *spec = NULL, *tmp_spec = NULL;
	
	HASH_ITER(hh, table->hash_table, spec, tmp_spec)
	{
		HASH_DEL(table->hash_table, spec);
		free(spec);
	}
    free(table);
}


void swarmkv_command_table_register(struct swarmkv_module *mod_table, const char *name, const char *hint,
						int arity, int key_offset, enum cmd_key_flag flag, enum key_not_found_reply failover, int auto_route,
						command_proc_func *proc, struct swarmkv_module *module)
{
    struct swarmkv_command_table *table = module2command_table(mod_table);
	struct swarmkv_cmd_spec *spec = NULL;

	spec = ALLOC(struct swarmkv_cmd_spec, 1);
	spec->name = name;
	spec->hint = hint;
	spec->arity = arity;
	spec->key_offset = key_offset;
	spec->flag = flag;
	spec->nokey_reply = failover;
	spec->proc = proc;
	spec->auto_route = auto_route;

	spec->module = module;
	HASH_ADD_KEYPTR(hh, table->hash_table, spec->name, strlen(spec->name), spec);
	return;
}
const struct swarmkv_cmd_spec *swarmkv_command_table_get_spec_by_argv(const struct swarmkv_module *mod_table, size_t argc, char* const argv[])
{
    struct swarmkv_command_table *table = module2command_table(mod_table);
	struct swarmkv_cmd_spec *spec=NULL;
	char name[256]="";
	strncpy(name, argv[0], sizeof(name));
	toUpper(name);
	HASH_FIND(hh, table->hash_table, name, strlen(name), spec);
	if(spec)
	{
		return spec;
	}
	if(argc<2)
	{
		return NULL;
	}
	snprintf(name, sizeof(name), "%s %s", argv[0], argv[1]);
	toUpper(name);
	HASH_FIND(hh, table->hash_table, name, strlen(name), spec);
	if(spec)
	{
		return spec;
	}
	return NULL;
}
size_t swarmkv_command_table_count(const struct swarmkv_module *mod_table)
{
    struct swarmkv_command_table *table = module2command_table(mod_table);
    return HASH_COUNT(table->hash_table);
}
size_t swarmkv_command_table_list_names(const struct swarmkv_module *mod_table, const char* cmd_names[], size_t sz)
{
    struct swarmkv_command_table *table = module2command_table(mod_table);
    struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
    size_t i = 0, count = 0;
    count = swarmkv_command_table_count(mod_table);
    assert(sz >= count);

    HASH_ITER(hh, table->hash_table, spec, tmp_spec)
    {
       cmd_names[i]=spec->name;
       i++;
    }
    assert(i == count);
    return i;
}
enum cmd_exec_result command_list_command(struct swarmkv_module *mod_command_table, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	size_t cnt = swarmkv_command_table_count(mod_command_table);
    const char *cmd_names[cnt];
    swarmkv_command_table_list_names(mod_command_table, cmd_names, cnt);
	*reply=swarmkv_reply_new_array(cnt);
	for(int i=0; i<cnt; i++)
	{
		(*reply)->elements[i]=swarmkv_reply_new_string(cmd_names[i], strlen(cmd_names[i]));
	}
	return FINISHED;
}
size_t swarmkv_command_table_find_possible_names(const struct swarmkv_module *mod_table, const char *prefix, const char* cmd_names[], size_t sz)
{
    struct swarmkv_command_table *table = module2command_table(mod_table);
	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	size_t n_matched=0;
	HASH_ITER(hh, table->hash_table, spec, tmp_spec)
	{
		if(0==strncasecmp(spec->name, prefix, strlen(prefix)) && n_matched<sz)
		{
			cmd_names[n_matched]=spec->name;
			n_matched++;
		}
	}
	return n_matched;
}
char *swarmkv_command_table_get_command_hint(const struct swarmkv_module *mod_table, const char *cmd_name)
{
   
    int argc, buflen = strlen(cmd_name);
    sds *argv = sdssplitargs(cmd_name,&argc);
    int endspace = buflen && isspace(cmd_name[buflen-1]);

    if (argc == 0)
    {
        sdsfreesplitres(argv,argc);
        return NULL;
    }
     struct swarmkv_command_table *table = module2command_table(mod_table);
    size_t i;
	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	HASH_ITER(hh, table->hash_table, spec, tmp_spec)
	{
        int argc_spec;
        sds *argv_spec = sdssplitargs(spec->name, &argc_spec);
        sds params=sdsdup(argv[0]);
        for(i=1; i<argc_spec && argc>=argc_spec; i++)
        {
            params = sdscatprintf(params, " %s", argv[i]);
        }
        sdsfreesplitres(argv_spec,argc_spec);

		if(0==strcasecmp(spec->name, params))
		{
            sds hint = sdsnew(spec->hint);
            int toremove = argc - argc_spec;
            while(toremove > 0 && sdslen(hint)) {
                if (hint[0] == '[') break;
                if (hint[0] == ' ') toremove--;
                sdsrange(hint,1,-1);
            }

            if (!endspace) {
                sds newhint = sdsnewlen(" ",1);
                newhint = sdscatsds(newhint,hint);
                sdsfree(hint);
                hint = newhint;
            }
            sdsfree(params);
            sdsfreesplitres(argv,argc);
			return hint;
		}
        sdsfree(params);
	}
    sdsfreesplitres(argv,argc);
	return NULL;
}
int command_spec_is_sufficient_args(const struct swarmkv_cmd_spec *spec, const struct swarmkv_cmd *cmd)
{
	size_t expect_argc=0;
	if(strchr(spec->name, ' '))
	{
		expect_argc=spec->arity+2;
	}
	else
	{
		expect_argc=spec->arity+1;
	}
	if(cmd->argc<expect_argc)
	{
		return 0;
	}
	return 1;
}