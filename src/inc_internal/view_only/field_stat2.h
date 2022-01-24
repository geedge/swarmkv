#ifndef H_SCREEN_STAT_H_INCLUDE
#define H_SCREEN_STAT_H_INCLUDE
#include <stdio.h>
#include <time.h>
#ifndef __cplusplus
#error("This file should be compiled with C++ compiler")
#endif

enum field_dsp_style_t
{
	FS_STYLE_FIELD=0,
	FS_STYLE_COLUMN,
	FS_STYLE_LINE,
	FS_STYLE_STATUS,
	FS_STYLE_HISTOGRAM
};
enum field_calc_algo
{
	FS_CALC_CURRENT=0,
	FS_CALC_SPEED
};
enum field_op
{
	FS_OP_ADD=1,
	FS_OP_SET,
	FS_OP_SUB
};

enum stats_output_format
{
	FS_OUTPUT_STATSD=1,
	FS_OUTPUT_INFLUX_LINE=2
};

enum metris_output_format
{
    FS_METRIS_OUTPUT_DEFAULT=0,
    FS_METRIS_OUTPUT_JSON=1
};

typedef	void* screen_stat_handle_t;

enum FS_option
{
	OUTPUT_DEVICE,		//VALUE is a const char*, indicate a  file path string, SIZE = strlen(string+'\0')+1.DEFAULT:output to stdout.
	PRINT_MODE,			//VALUE is an interger,1:Rewrite ,2: Append. SIZE=4,DEFALUT:REWRITE.
	STAT_CYCLE,			//VALUE is an interger idicate interval seconds of every output, SIZE=4 ,DEFUALT:2 seconds.
	PRINT_TRIGGER,		//VALUE is an interger,1:Do print,0: Don't print.SIZE=4.DEFAULT:1.
	CREATE_THREAD,		//VALUE is an interger,1: Create a print thread,0:not create,output by call passive_output function,
						//and the STAT_CYCLE is meaningless.SIZE=4,DEFAULT:0.
	ID_INVISBLE,		//value is field_id/status_id/column_id, not output this string, SIZE=4,DEFAULT: shutdown NO one.
	FLUSH_BY_DATE,		//value is 1(true) or 0(false),SIZE=4,DEFAULT: Do not flush by date.
	APP_NAME,			//VALUE is a const char*, MUST end with '\0', SIZE= strlen(string+'\0')+1. DEFAULT is "?".
	STATS_SERVER_IP, 	//VALUE is a const char*, MUST end with '\0', SIZE= strlen(string+'\0')+1. No DEFAULT.
	STATS_SERVER_PORT, 	//VALUE is a unsigned short or a signed int, host order, SIZE= sizeof(unsigned short) or sizeof(int). No DEFAULT.
	STATS_FORMAT,		//VALUE is enum stats_output_format, STATSD or INFLUX_LINE, SIZE=sizeof(int), DEFAULT:STATSD.
	MAX_STAT_FIELD_NUM,		//VALUE is an interger, SIZE=sizeof(int), DEFAULT:1024.
	HISTOGRAM_GLOBAL_BINS,	//VALUE is a const char*, define a histogram bins for default field, SIZE = strlen(string+'\0')+1. DEFAULT: "0.5,0.8,0.9,0.95,0.99“.
	NOT_SEND_METRIC_TO_SERVER,		//value is field_id/status_id/column_id, not output this string, SIZE=4,DEFAULT:Send.
	METRIS_FORMAT, //VALUE is enum metris_output_format, DEFAULT or JSON, SIZE=sizeof(int).
	OUTPUT_PROMETHEUS	//VALUE is an interger,1:output prometheus ,0: not output. SIZE=4,DEFALUT: 0.
};

//Always success.
screen_stat_handle_t FS_create_handle(void);

int FS_set_para(screen_stat_handle_t handle, enum FS_option  type,const void* value,int size);
void FS_start(screen_stat_handle_t handle);
void FS_stop(screen_stat_handle_t* handle);

//return field_id/line_id/column_id greater than zero if success,return an interger less than zero if failed.
//should NOT include "|:\n\r.\t<>[]#!@"or space in the parameter name.
//Runtime rregister column is NOT allowed.
int FS_register(screen_stat_handle_t handle,enum field_dsp_style_t style,enum field_calc_algo calc_type,const char* name);

//numerator_id and denominator_id must be column/field/status style.
//scaling: negative value: zoom in; positive value: zoom out;
int FS_register_ratio(screen_stat_handle_t handle,int numerator_id,int denominator_id,int scaling,enum field_dsp_style_t style,enum field_calc_algo calc_type,const char* name);

inline void record_time_start(struct timespec *start)
{
	clock_gettime(CLOCK_MONOTONIC, start);
}
inline long record_time_elapse_us(struct timespec *start)
{
	struct timespec end;
	long elapsed = 0;
	clock_gettime(CLOCK_MONOTONIC, &end);
	elapsed = (end.tv_sec - start->tv_sec)*1000000 + (end.tv_nsec - start->tv_nsec)/1000;
	return elapsed;
}

//@param bins format is comma spited number, e.g."0.1,0.5,0.8,0.9,0.95,0.99"
//return 0 on success, <0 on failed.
int FS_histogram_set_bins(screen_stat_handle_t handle, int id, const char* bins);

//@param lowest_trackable_value >1
//@param highest_trackable_value>lowest_trackable_value * 2 
//@param 1<significant_figures<4
int FS_register_histogram(screen_stat_handle_t handle, enum field_calc_algo calc_type, const char* name,
		long long lowest_trackable_value,
        long long highest_trackable_value,
        int significant_figures);

//id: when id's type is FIELD , column_id is ignored.
int FS_operate(screen_stat_handle_t handle,int id,int column_id,enum field_op op,long long value);

void FS_passive_output(screen_stat_handle_t handle);

//@param default port is 9273
//return 1 on success, 0 on failed.
int FS_library_set_prometheus_port(unsigned short port);
//@param default url_path is "/metrics"
//return 1 on success, 0 on failed.
int FS_library_set_prometheus_url_path(const char *url_path);
int FS_library_init(void);

#endif

