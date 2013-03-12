#include <stdio.h>
#include <stdlib.h>

#include "uv.h"

struct peer {
	uv_tcp_t tcpsock;
#ifdef SRV
	struct sockaddr_in addr; // Client's current addr
#endif
};

uv_loop_t *loop;
