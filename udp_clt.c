// -*- mode: C; c-basic-offset: 3; tab-width: 8; indent-tabs-mode: nil -*-

#define CLT
#include "udp_comm.c"

void on_new_connection (uv_stream_t *server, int status) {
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
      peer_start (peer, uv_ip4_addr ("127.0.0.1", 9020), -1);
   }
   else {
      uv_close ((uv_handle_t*) &peer->tcpsock, NULL);
      free (peer);
   }
}

int main () { 
   loop = uv_default_loop ();

   uv_tcp_t server;
   uv_tcp_init (loop, &server);

   /* Set up a listener and start a new machine for each
    * incoming connection. (I'm not going into stdin/out
    * right now, libuv isn't scary but...)
    */
   struct sockaddr_in bind_addr = uv_ip4_addr ("0.0.0.0", 2222);
   uv_tcp_bind (&server, bind_addr);
   int r = uv_listen ((uv_stream_t*) &server, 128, on_new_connection);
   if (r) {
      fprintf (stderr, "Listen error %s\n",
               uv_err_name (uv_last_error (loop)));
      return 1;
   }
   return uv_run (loop, UV_RUN_DEFAULT);
}
