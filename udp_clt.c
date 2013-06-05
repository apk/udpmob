// -*- mode: C; c-basic-offset: 3; tab-width: 8; indent-tabs-mode: nil -*-

#define CLT
#include "udp_comm.c"

void echo_write (uv_write_t *req, int status) {
   if (status == -1) {
      fprintf (stderr, "Write error %s\n",
               uv_err_name (uv_last_error (loop)));
   }
   char *base = (char*) req->data;
   free (base);
   free (req);
}

void echo_read (uv_stream_t *client, ssize_t nread, uv_buf_t buf) {
   fprintf (stderr, "Read %d\n", (int)nread);
   if (nread == -1) {
      if (uv_last_error (loop).code != UV_EOF)
         fprintf (stderr, "Read error %s\n",
                  uv_err_name (uv_last_error (loop)));
      uv_close ((uv_handle_t*) client, NULL);
      return;
   }

   uv_write_t *req = malloc (sizeof (uv_write_t));
   req->data = (void*) buf.base;
   uv_write (req, client, &buf, 1, echo_write);
}

void on_new_connection (uv_stream_t *server, int status) {
   if (status == -1) {
      // error!
      return;
   }

   peer_t *peer = malloc (sizeof (peer_t));
   uv_tcp_init (loop, &peer->tcpsock);
   if (uv_accept (server, (uv_stream_t*) &peer->tcpsock) == 0) {
      // uv_read_start ((uv_stream_t*) &peer->tcpsock, alloc_buffer, echo_read);
      start_peer (peer);
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
