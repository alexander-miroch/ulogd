#ifndef _ULOGD
#define _ULOGD

#include <getopt.h>
#include <pthread.h>
#include <time.h>

#define DEFAULT_WORKDIR 		"/"
#define DEFAULT_PIDFILE 		"/var/run/ulogd.pid"

#define DEFAULT_MIN_THREADS 		4
#define DEFAULT_MAX_THREADS 		16

#define DEFAULT_STACK_SIZE 		8192

#define DEFAULT_RR_PRIORITY 		50
#define DEFAULT_NUMBER_ENTRIES		40

#define UMASK				0022

#define DEFAULT_PORT 			5001
#define DEFAULT_BUFFER_SIZE 		2097136

#define MAX_LOG_STRING_SIZE 		4096
//#define MAX_LOG_STRING_SIZE 		300
//#define MAX_RECORDS_PER_BUFFER		1024
#define MAX_RECORDS_PER_BUFFER		100

#define DEFAULT_DOM_LEN			64

#define TOTAL_BUF_SIZE			(MAX_LOG_STRING_SIZE * MAX_RECORDS_PER_BUFFER)

#define DEFAULT_FLUSHER_INTERVAL	30	

#define DEFAULT_USER_DIR		"/var/www/root"

extern char *optarg;
extern int optind;

typedef struct {
	int start;
	int min;
	int cur;
	int max;
} tpargs_t;

struct thr_el_t {
	pthread_t tid;
	int 	  num;
	int 	  err;
	int 	  served;
	char buf[MAX_LOG_STRING_SIZE];
	char *domain;
	char *log;
	struct thr_el_t *next;
}; 

struct hash_el_t {
	unsigned char *domain;
	char *buffer;
	pthread_mutex_t buf_mtx;
	int pos;
	time_t last_accessed;
	struct hash_el *next;
};

struct thr_el_t **tmain;

struct thr_el_t *find_me(pthread_t);

void create_thread_pool(void);
void create_flush_thread(void);
void usage(short int);

void start_daemon(void);
void sig_h(int);
void *main_loop(void *);
void *flush_task(void *);
void do_flush(unsigned char *,char *,int);
void thread_cleanup(void *);

int setup_network(void);
int handle_req(struct thr_el_t *);
unsigned int hash0(unsigned char *, size_t);
int mbuf_init(void);
int add_mbuf_entry(unsigned int,struct hash_el_t *);
struct hash_el_t *create_mbuf_entry(char *);


void list(void);
#endif
