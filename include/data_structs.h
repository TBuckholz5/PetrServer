#ifndef DATASTRUCTS_H
#define DATASTRUCTS_H

#include <stdint.h>
#include <stdlib.h>
#include "linkedlist.h"

typedef struct {
    char name[1024];
    int connfd;
} user;

typedef struct {
    char name[1024];
    user *creator;
    List_t userlist;
} room;

typedef struct {
    uint8_t msg_type;
    user* sender;
    char userrec[1024];
    char roomrec[1024];
    char msg[1024];
} job;

List_t userlist;
List_t roomlist;


#endif
