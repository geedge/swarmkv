struct vc_element
{
	uuid_t id;
	long long counter;
	UT_hash_handle hh;
};
struct vector_clock
{

};
struct vector_clock *vector_clock_new(uuid_t my_id)
{
}
void vector_clock_tick(struct vector_clock* vc)
{

}
int vector_clock_compare(struct vector_clock *vc, sds remote_blob)
{

}
sds vector_clock_to_blob(struct vector_clock *vc)
{

}
