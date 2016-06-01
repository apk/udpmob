// -*- mode: C; c-basic-offset: 3; tab-width: 8; indent-tabs-mode: nil -*-

static char *remaddr = "127.0.0.1";
static int remport = 9020;

#define CLT
#include "udp_comm.c"

struct clt {
   uv_tcp_t server;
   int sel;
};

void on_new_connection (uv_stream_t *server, int status) {
   struct clt *clt = server->data;
   if (status == -1) {
      // error!
      return;
   }

   /* Each new connection: Start up a
    * peer object for this connection.
    */
   peer_t *peer = malloc (sizeof (peer_t));
   uv_tcp_init (loop, &peer->tcpsock);
   if (uv_accept (server, (uv_stream_t*) &peer->tcpsock) == 0) {
      struct sockaddr_in sin;
      uv_ip4_addr (remaddr, remport, &sin);
      peer_start (peer, (struct sockaddr_in *)&sin, clt->sel, 1);
      peer_open (peer);
   }
   else {
      uv_close ((uv_handle_t*) &peer->tcpsock, NULL);
      free (peer);
   }
}

static void make_server (int port, int sel) {
   struct clt *clt = malloc (sizeof (struct clt));

   uv_tcp_init (loop, &clt->server);
   clt->server.data = clt;

   /* Set up a listener and start a new machine for each
    * incoming connection. (I'm not going into stdin/out
    * right now, libuv isn't scary but...)
    */
   struct sockaddr_in bind_addr;
   uv_ip4_addr ("0.0.0.0", port, &bind_addr);
   uv_tcp_bind (&clt->server, (struct sockaddr *)&bind_addr, 0);
   int r = uv_listen ((uv_stream_t*) &clt->server, 128, on_new_connection);
   if (r) {
      fprintf (stderr, "Listen error %s\n",
               uv_err_name (r));
      exit (1);
   }
}

int main (int argc, char **argv) {
   int i;
   int cnt = 0;
   int localport;

   loop = uv_default_loop ();

   for (i = 1; i < argc; i ++) {
      switch (argv [i] [0]) {
      case 'a':
         remaddr = argv [i] + 1;
         break;
      case 'p':
         remport = atoi (argv [i] + 1);
         if (!remport) remport = 9000;
         break;
      case 'l':
         localport = atoi (argv [i] + 1);
         if (!localport) localport = 2222;
         make_server (localport, cnt ++);
         break;
      case 'd':
         debug = atoi (argv [i] + 1);
         break;
      default:
         fprintf (stderr, "Bad arg %s\n", argv [i]);
         break;
      }
   }

   fprintf (stderr, "%d:%s:%d\n", localport, remaddr, remport);

   return uv_run (loop, UV_RUN_DEFAULT);
}
