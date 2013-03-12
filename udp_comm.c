#include <stdio.h>
#include <stdlib.h>

#include "uv.h"

uv_loop_t *loop;

typedef struct peer {
	uv_tcp_t tcpsock;
	struct sockaddr_in addr; // Peer addr (current one, for server)
#ifdef CLT
	uv_timer_t timer;
#endif
#ifdef CLT
	uv_udp_t udp;
#endif
} peer_t;

#ifdef CLT
void on_send (uv_udp_send_t* req, int status) {
	fprintf (stderr, "on_send %d\n", status);
}

void fire (uv_timer_t* handle, int status) {
	peer_t *p = handle->data;
	uv_udp_send_t *req = malloc (sizeof (uv_udp_send_t));
	uv_buf_t *buf = malloc (sizeof (uv_buf_t));
	buf [0] = uv_buf_init (malloc (1), 0);
	fprintf (stderr, "fire %d\n", status);
	uv_udp_send (req, &p->udp, buf, 1, p->addr, on_send);
}

void start_peer (peer_t *p) {
	// Assume tcpsock already set up (non-reading)
	uv_timer_init (loop, &p->timer);
	p->timer.data = p;
	uv_timer_start (&p->timer, fire, 1000, 0);
	p->addr = uv_ip4_addr ("127.0.0.1", 9020);
	fprintf (stderr, "start_peer\n");

	uv_udp_init (loop, &p->udp);
	uv_udp_bind (&p->udp, uv_ip4_addr("0.0.0.0", 0), 0);
}
#endif
