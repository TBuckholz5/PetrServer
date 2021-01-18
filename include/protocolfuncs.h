#ifndef PROTOCOLFUNCS_H
#define PROTOCOLFUNCS_H

#include <semaphore.h>
#include <string.h>
#include <unistd.h>
#include "data_structs.h"
#include "protocol.h"

int usercnt, roomcnt;
sem_t usermutex, userw, roommutex, roomw;

void protocolfuncsinit(void);
void login(user *newuser);
void logout(user *olduser, bool terminated);
void rmcreate(user *usercreator, char *newroomname);
void rmdelete(char *oldroom);
void rmlist(user *thisuser);
void rmjoin(user *newuser, char *roomname);
void rmleave(user *newuser, char *roomname);
void rmsend(user *newuser, char *roomname, char *roommsg);
void usrsend(user *sender, char *recipient, char *usermsg);
void usrlist(user *thisuser);

#endif
