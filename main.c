/*
*
* 
*
*
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>    
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h> 
#include <netdb.h>
#include <errno.h>

#include "ulogd.h"

int d;
char *wdir = NULL;
char *pidfile = NULL;
int port = DEFAULT_PORT;
int sk;
char *udir = DEFAULT_USER_DIR;
unsigned int buffsize = DEFAULT_BUFFER_SIZE;
unsigned int entries = DEFAULT_NUMBER_ENTRIES;
unsigned int flush_ivl = DEFAULT_FLUSHER_INTERVAL;

int created = 0;
pthread_mutex_t recv_m = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cond_m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_v = PTHREAD_COND_INITIALIZER;

tpargs_t tpargs;
static struct option opts[] = {
        {"help",0,0,'h'},
        {"usage",0,0,'h'},
        {"daemon",0,0,'d'},
        {"workdir",0,0,'w'},
        {"pidfile",0,0,'f'},
        {"port",1,0,'p'},
        {"buffer",1,0,'b'},
        {"userdir",1,0,'r'},
        {"kentries",1,0,'e'},
        {"interval",1,0,'i'},
        {"sthreads",1,&tpargs.start,-1},
        {"minthreads",1,&tpargs.min,-1},
        {"maxthreads",1,&tpargs.max,-1},
        {0,0,0,0}
};

void usage(short int es) {
	FILE *f;

	f = (es) ? stderr : stdout;
	fprintf(f,"Usage:\n\
		\t-h help\n\
		\t-u usage\n\
		\t-d daemon\n\
		\t-e <num> oriented K number of domains\n\
		");
	exit(es);
}

void start_daemon(void) {
	long fds;
	register int j;
	FILE *f;

	if (getppid() == 1) return;

	switch(fork()) {
		case 0:
			break;
		case -1:
			fprintf(stderr,"Fork error,exitting...\n");
			exit(1);
		default:
			exit(0);
	}

	umask(UMASK);
	f = fopen(pidfile,"w");
	if (!f) {
		fprintf(stderr,"Cant't open pidfile %s\n",pidfile);
		exit(1);
	}
	fprintf(f,"%d",getpid());
	fclose(f);

	setsid();
	if (chdir(wdir) < 0) {
		fprintf(stderr,"Can't chdir to %s\n",wdir);
		exit(1);
	}
	fds = sysconf(_SC_OPEN_MAX);
	for (j=0;j<fds;++j) close(j);
	
	j = open("/dev/null",O_RDWR);
	if (j>=0) {
		dup(j);	
		dup(j);
	}
	
	signal(SIGCHLD,SIG_IGN); 
	signal(SIGTSTP,SIG_IGN); 
	signal(SIGTTOU,SIG_IGN);
	signal(SIGTTIN,SIG_IGN);
	//signal(SIGINT,SIG_IGN);
	signal(SIGHUP,sig_h);
	signal(SIGTERM,sig_h);
}

void sig_h(int sig) {
	switch (sig) {
		case SIGHUP:
			syslog(LOG_INFO,"sighup received");
			break;
		case SIGTERM:
			syslog(LOG_INFO,"sigterm received");
			unlink(pidfile);
			exit(0);
			break;
	}
}

int main(int argc,char *argv[]) {
	char c;
	int odx;

	tpargs.start = tpargs.min = DEFAULT_MIN_THREADS;
	tpargs.max = DEFAULT_MAX_THREADS;
	tpargs.cur = 0;
	
	while ((c = getopt_long(argc,argv,"hudw:p:b:e:i:r:",opts,&odx)) != -1) {
		switch (c) {
			case 'h':
			case 'u':
				usage(0);
				break;
			case 'd':
				d = 1;
				break;
			case 'w':
				wdir = (char *) strdup(optarg);
				break;		
			case 'f':
				pidfile = (char *) strdup(optarg);
				break;
			case 'r':
				udir = (char *) strdup(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'i':
				flush_ivl = atoi(optarg);
				break;
			case 'b':
				buffsize = atoi(optarg);
				break;
			case 'e':
				entries = atoi(optarg);
				break;
			case 0:
				if (tpargs.start == -1) tpargs.start = atoi(optarg);
				if (tpargs.min == -1) tpargs.min = atoi(optarg);
				if (tpargs.max == -1) tpargs.max = atoi(optarg);
				break;
			case '?':
			default:
				usage(1);
		}
	}


	if (getuid()) {
		fprintf(stderr,"Only root can run it\n");
		exit(0);
	}

	openlog("ulogd",0,LOG_LOCAL6);
	syslog(LOG_INFO,"Starting server");

	if (port < 1 || port > 65535) {
		syslog(LOG_ERR,"Invalid port\n");
		exit(1);
	}

	if (flush_ivl < 1 || flush_ivl > 3600 * 24) {
		syslog(LOG_ERR,"Invalid flush interval\n");
		exit(1);
	}

	if (entries > 0xffffffff / 1024) {
		syslog(LOG_INFO,"Too much number entries\n");
		exit(1);
	}

	if (tpargs.min > tpargs.start || tpargs.start > tpargs.max) {
		syslog(LOG_ERR,"Invalid thread count\n");
		exit(1);
	}

	if (!pidfile) pidfile = DEFAULT_PIDFILE;
	if (!wdir) wdir = DEFAULT_WORKDIR;
	if (d) start_daemon();

	syslog(LOG_INFO,"Creating thread pool");

	if (setup_network() < 0) {
		syslog(LOG_ERR,"Network configuration failed");
		kill(getpid(),SIGTERM);
	}

	entries *= 1024;
	if (mbuf_init() < 0) {
		syslog(LOG_ERR,"Mbuf init failed");
		kill(getpid(),SIGTERM);
	}

	create_thread_pool();
	list();

	/* OK. Start threads are created, we can to begin */
	pthread_mutex_lock(&cond_m);
	created = 1;
	pthread_cond_broadcast(&cond_v);
	pthread_mutex_unlock(&cond_m);

	create_flush_thread();

	while (1) {
		sleep(1);
	}

}

int setup_network(void) {
	struct sockaddr_in sa;
	int pv,v;
	socklen_t slen = sizeof(int);

	sk = socket(AF_INET,SOCK_DGRAM,0);
	if (sk < 0) {
		syslog(LOG_ERR,"Can't create socket");
		return -1;
	}
	memset(&sa,0,sizeof(struct sockaddr_in));
	sa.sin_family = PF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = INADDR_ANY;

	if (bind(sk,&sa,sizeof(struct sockaddr_in)) < 0) {
		syslog(LOG_ERR,"Can't bind to 0.0.0.0:%d\n",port);
		return -1;
	}

	pv = v = buffsize;
	if (setsockopt(sk,SOL_SOCKET,SO_RCVBUF,&v,slen) < 0) {
		syslog(LOG_ERR,"setsockopt error");
		return -1;
	}
	if (getsockopt(sk,SOL_SOCKET,SO_RCVBUF,&v,&slen) < 0) {
		syslog(LOG_ERR,"getsockopt error");
		return -1;
	}

	if (pv > v) {
		syslog(LOG_WARNING,"UDP buffer can be too small, set rmem_max to %d",pv/2);
	}

	syslog(LOG_INFO,"Waiting on port %d\n",port);
	return 0;
}

void create_flush_thread(void) {
	pthread_attr_t p_attr;
	pthread_t tid;
	struct sched_param sp;

	pthread_attr_init(&p_attr);
	pthread_attr_setstacksize(&p_attr,DEFAULT_STACK_SIZE);
	pthread_attr_setdetachstate(&p_attr,PTHREAD_CREATE_DETACHED);
	pthread_attr_setschedpolicy(&p_attr,SCHED_RR);
	sp.sched_priority = DEFAULT_RR_PRIORITY;
	pthread_attr_setschedparam(&p_attr,&sp);
	if (pthread_create(&tid,&p_attr,flush_task,NULL)) {
		syslog(LOG_ERR,"Can't create flush thread");
		kill(getpid(),SIGTERM);
	}
	pthread_attr_destroy(&p_attr);
}

void create_thread_pool(void) {
	int i;
	int j;
	int ft = 1;
	struct thr_el_t *thr_el,*prev_thr,**begin;
	pthread_attr_t p_attr;
	pthread_t tid;
	
	pthread_attr_init(&p_attr);
	pthread_attr_setstacksize(&p_attr,DEFAULT_STACK_SIZE);
	pthread_attr_setdetachstate(&p_attr,PTHREAD_CREATE_DETACHED);

	begin = tmain = (struct thr_el_t **) malloc(sizeof(struct thr_el_t *) * tpargs.max);
	if (!tmain) {
		syslog(LOG_ERR,"No memory");
		kill(getpid(),SIGTERM);
	}
	prev_thr = 0;
	for (i=0,j=0; i<tpargs.start; ++i) {
		if (!pthread_create(&tid,&p_attr,main_loop,NULL)) {
			thr_el = (struct thr_el_t *) malloc (sizeof(struct thr_el_t));
			if (ft) {
				*begin = thr_el;
				ft = 0;
			}
			if (!thr_el) continue;
			thr_el->tid = tid;
			thr_el->num = j;
			thr_el->next = 0;
			thr_el->served = 0;
			if (prev_thr)
 				prev_thr->next = thr_el;

			prev_thr = thr_el;
			j++;
		}
	}
	if (i != j) {
		syslog(LOG_WARNING,"Only %d of %d threads are created\n",j,tpargs.start);
	}
	tpargs.cur = j;
	pthread_attr_destroy(&p_attr);
	tmain = begin;
	return;
}

void list(void) {
	struct thr_el_t *f,**begin;
	
	begin = tmain;
	for (f = *begin; f; f = f->next) {
		//syslog(LOG_INFO,"tnum-%d,tid=%d,next=%x\n",f->num,f->tid,f->next);
	}
}

