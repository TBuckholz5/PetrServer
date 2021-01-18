#ifndef SERVER_H
#define SERVER_H

#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "sbuf.h"
#include "protocol.h"
#include "protocolfuncs.h"

#define BUFFER_SIZE 1024
#define SA struct sockaddr

#define USAGE "./bin/petr_server [-h][-j N] PORT_NUMBER ADUIT_FILENAME\n\n" \
              "-h\t\tDisplays this help menu, and returns EXIT_SUCCESS\n"   \
              "-j N\t\tNumber of job threads. Default to 2.\n"              \
              "AUDIT_FILENAME\tFile to output Audit Log Messages to.\n"     \
              "PORT_NUMBER\tPort number to listen on.\n"

void run_server(int server_port);

#endif
