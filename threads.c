#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "ulogd.h"

extern pthread_mutex_t recv_m;
extern pthread_mutex_t cond_m;
extern pthread_mutex_t mbuf_mtx;
extern pthread_cond_t cond_v;
extern int created;
extern int sk;
extern unsigned int entries;
extern struct hash_el_t **mbuf;
extern unsigned int flush_ivl;
extern char *udir;

pthread_mutex_t ts_m;

void thread_cleanup(void *arg) {
	syslog(LOG_INFO,"thread %d is exitted\n",(int)pthread_self());
	pthread_mutex_unlock(&recv_m);
}

void *main_loop(void *args) {
	void *mem = 0;
	int rv;
	struct thr_el_t *me;
	char *tp;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED,NULL);
	pthread_cleanup_push(thread_cleanup,mem);
	pthread_testcancel();
	
	pthread_mutex_lock(&cond_m);
	while (!created)
		pthread_cond_wait(&cond_v,&cond_m);
	pthread_mutex_unlock(&cond_m);

	pthread_testcancel();
	me = find_me(pthread_self());
	if (!me) {
		pthread_exit(0);
	}
	pthread_testcancel();

	while (1) {
		pthread_mutex_lock(&recv_m);
		rv = recv(sk,me->buf,MAX_LOG_STRING_SIZE - 1,0);
		pthread_mutex_unlock(&recv_m);
		if (rv < 0) {
			me->err++;
			continue;
		}
		tp = strchr(me->buf,'>');
		if (!tp) {
			me->err++;
			continue;
		}
		*tp++ = 0;

		me->buf[rv-1] = 0;	
		me->domain = me->buf;
		me->log = tp;
		if (strlen(me->domain)>DEFAULT_DOM_LEN) {
			syslog(LOG_WARNING,"Domain name is too long %s\n",me->domain);
			continue;
		}
		if (handle_req(me) < 0) {
			me->err++;
			continue;
		}
		me->served++;
	}
	pthread_cleanup_pop(1);
}

int handle_req(struct thr_el_t *me) {
	unsigned int hash;
	struct hash_el_t *new;
	struct hash_el_t *tmp;
	int len;

	hash = hash0((unsigned char *)me->domain,strlen(me->domain));
	hash %= entries;

	pthread_mutex_lock(&mbuf_mtx);
	tmp = mbuf[hash];
	//if (tmp)
	//	syslog(LOG_INFO,"tmp=%x-post=%d nbext=%d\n ",tmp,tmp->pos,tmp->next);
	pthread_mutex_unlock(&mbuf_mtx);

	if (!tmp) {
		new = create_mbuf_entry(me->domain);
		if (!new) {
			syslog(LOG_WARNING,"Error creating new struct");
			return -1;
		}
		
		/*
		 * TODO: Collisions check, hashtable already disigned with single-linked list of domains
		 * with equal hashes
		 */
		pthread_mutex_lock(&mbuf_mtx);
		/* One more check for exluding race condition */
		tmp = mbuf[hash];
		if (!tmp)
			add_mbuf_entry(hash,new);
		tmp = mbuf[hash];
		pthread_mutex_unlock(&mbuf_mtx);
	}
	len = strlen(me->log);
	if (!len) return -1;

	pthread_mutex_lock(&tmp->buf_mtx);
	if (tmp->pos + len >= TOTAL_BUF_SIZE) {
		/* Need to flush */
		do_flush(tmp->domain,tmp->buffer,tmp->pos);
		tmp->pos = 0;
	}
	memcpy(tmp->buffer + tmp->pos,me->log,len);
	memcpy(tmp->buffer + tmp->pos + len,"\n",1);
	tmp->pos += len + 1;
	time(&tmp->last_accessed);
	pthread_mutex_unlock(&tmp->buf_mtx);

	return 0;
}

struct thr_el_t *find_me(pthread_t tid) {
	struct thr_el_t *f,**begin;

	pthread_mutex_lock(&ts_m);
	begin = tmain;
	for (f = *begin; f; f = f->next) {
		if (f->tid == tid) {
			pthread_mutex_unlock(&ts_m);
			return f;
		}
	}
	pthread_mutex_unlock(&ts_m);
	return 0;
}

void *flush_task(void *args) {
	struct hash_el_t *tmp;
	register int i;

	while (1) {
		syslog(LOG_INFO,"flush thread %d sleeping for %d\n",(int)pthread_self(),flush_ivl);
		pthread_mutex_lock(&mbuf_mtx);
		for (i=0;i<entries; i++) {
			if (mbuf[i]) {
				tmp = mbuf[i];
				pthread_mutex_unlock(&mbuf_mtx);
				// try lock
				pthread_mutex_lock(&tmp->buf_mtx);
				if (!tmp->pos) {
					if (time(NULL) - tmp->last_accessed > 36) {
						syslog(LOG_INFO,"Expired, destroying %s\n",tmp->domain);
						free(tmp->domain);
						free(tmp->buffer);
						tmp->last_accessed = 0;
						pthread_mutex_destroy(&tmp->buf_mtx);
						pthread_mutex_lock(&mbuf_mtx);
						mbuf[i] = 0;
						continue;
					}
					pthread_mutex_unlock(&tmp->buf_mtx);
					pthread_mutex_lock(&mbuf_mtx);
					continue;
				}
				do_flush(tmp->domain,tmp->buffer,tmp->pos);
				tmp->pos = 0;
				pthread_mutex_unlock(&tmp->buf_mtx);
				pthread_mutex_lock(&mbuf_mtx);
			}
		}
		pthread_mutex_unlock(&mbuf_mtx);
		syslog(LOG_INFO,"Finished %x\n",i);
		
		sleep(flush_ivl);
	}
	return 0;
}

void do_flush(unsigned char *domain,char *buffer,int len) {
	char filename[1024],dirname[1024],rdir[1024],*tp,*dp;
	struct stat s;
	int rv;
	FILE *fd;
	
	dp = (char *) strrchr((char *)domain,':');
	if (dp) *dp = 0;

	dp = (char *) strrchr((char *)domain,'.');
	if (!dp) {
		syslog(LOG_WARNING,"Invalid domain %s %s\n",domain,buffer);
		return;
	}
	*dp = 0;
	++dp;
	snprintf(dirname,512,"%s/%s/%s/statistics/logs",udir,dp,(char *)domain);
	rv = stat(dirname,&s);
	if (!rv) {
		snprintf(filename,1024,"%s/access_log",dirname);
	} else {
		snprintf(dirname,512,"%s/%s/%s",udir,dp,(char *)domain);
		if (!realpath(dirname,rdir)) {
			dp--;
			*dp = '.';
			syslog(LOG_WARNING,"Can't calc realpath for %s\n",dirname);
			return;
		}
		tp = strstr(rdir,"/subdomains");
		if (!tp) return;
		*tp = 0;
		tp += strlen("/subdomains");
		*tp++ = 0;
		snprintf(filename,1024,"%s/statistics/logs/%s-access_log",rdir,tp);
	}
	dp--;
	*dp = '.';

	fd = fopen(filename,"a+");
	if (!fd) {
		syslog(LOG_WARNING,"Can't flush for %s. Error open %s\n",domain,filename);
		return;
	}
	rv = fwrite(buffer,len,1,fd);
	if (rv != 1) {
		fclose(fd);
		syslog(LOG_WARNING,"Can't flush for %s. Error write %s %d-%d\n",domain,filename,ferror(fd),rv);
		return;
	}
	fclose(fd);

}

