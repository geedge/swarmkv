struct swarmkv_rpc
{
	unsigned long long sequence;
	node_t peer;
	struct event *timeout_ev;
	on_reply_callback_t *cb;
	void *cb_arg;
	struct snet_thread *thr;
	struct timespec start;
	UT_hash_handle hh;
};