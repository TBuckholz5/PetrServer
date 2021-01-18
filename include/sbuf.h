#ifndef SBUF_H
#define SBUF_H

#include <semaphore.h>
#include <stdlib.h>
#include "data_structs.h"


typedef struct {
    job **buf;      // Buffer array
    int n;          // Max number of slots
    int front;      // buf[(front+1)%n] is first item
    int rear;       // buf[rear%n] is last item
    sem_t mutex;    // Protects access to buf
    sem_t slots;    // Counts available slots
    sem_t items;    // Counts available items
} sbuf_t;

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, job *item);
job *sbuf_remove(sbuf_t *sp);

#endif
