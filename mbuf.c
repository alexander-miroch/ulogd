#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <pthread.h>

#include "ulogd.h"

extern unsigned int entries;
struct hash_el_t **mbuf;
pthread_mutex_t mbuf_mtx = PTHREAD_MUTEX_INITIALIZER;

int mbuf_init(void) {
	unsigned long size;
	syslog(LOG_INFO,"Initializing main buffer with %d entries\n",entries);

	size = sizeof(struct hash_el_t);
	syslog(LOG_INFO,"size=%lu total=%lu\n",size,size*entries);
	/* No calloc here */
//	mbuf = (unsigned long *) malloc(sizeof(unsigned long) * entries);
	mbuf = (struct hash_el_t **) malloc(size * entries);
	if (!mbuf) {
		syslog(LOG_ERR,"No mem for mbuf, try to use --kentries option\n");
		return -1;
	}
	bzero(mbuf,entries * size);
	return 0;
}

struct hash_el_t *create_mbuf_entry(char *name) {
	struct hash_el_t *new;

	new = (struct hash_el_t *) malloc(sizeof(struct hash_el_t));
	if (!new) return (struct hash_el_t *) 0;

	new->buffer = (char *) malloc(sizeof(char) * TOTAL_BUF_SIZE);
	if (!new->buffer) return 0;

	new->domain = (unsigned char *)strdup(name);
	new->pos = 0;
	new->last_accessed = 0;
	new->next = 0;
	if (pthread_mutex_init(&new->buf_mtx,0)) return (struct hash_el_t *) 0;
	
	return new;
}

int add_mbuf_entry(unsigned int hash,struct hash_el_t *el) {
	
	/* TODO: Collision check */
	mbuf[hash] = el;	
	return 0;
}

unsigned int hash0(unsigned char *key, size_t key_len)
{
    unsigned int hash = 0;
    size_t i;

    for (i = 0; i < key_len; i++) {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);

    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

