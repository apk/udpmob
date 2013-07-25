// -*- mode: C; c-basic-offset: 3; tab-width: 8; indent-tabs-mode: nil -*-

static char *remaddr = "127.0.0.1";
static int remport = 22;
static int localport = 9020;

#include "udp_comm.c"

uv_loop_t *loop;

#if 0
void on_read(uv_udp_t *req, ssize_t nread, uv_buf_t buf, struct sockaddr *addr, unsigned flags) {

   fprintf (stderr, "nread: %d\n", (int)nread);
   if (nread == -1) {
      fprintf(stderr, "Read error %s\n", uv_err_name(uv_last_error(loop)));
      uv_close((uv_handle_t*) req, NULL);
      free(buf.base);
      return;
   }

   if (addr) {
      char sender[17] = { 0 };
      uv_ip4_name((struct sockaddr_in*) addr, sender, 16);
      fprintf(stderr, "Recv from %s\n", sender);
   } else {
      fprintf(stderr, "Recv from unknown\n");
   }

   dumpbuf("B", buf, nread);

   // ... DHCP specific code

   // above comment is only for book code snippet purposes

#if 0
   unsigned int *as_integer = (unsigned int*)buf.base;
   unsigned int ipbin = ntohl(as_integer[4]);
   unsigned char ip[4] = {0};
   int i;
   for (i = 0; i < 4; i++)
      ip[i] = (ipbin >> i*8) & 0xff;
   fprintf(stderr, "Offered IP %d.%d.%d.%d\n", ip[3], ip[2], ip[1], ip[0]);
#endif

   free(buf.base);
#if 0
   uv_udp_recv_stop(req);
#endif
}
#endif

int main (int argc, char **argv) {
   int i;

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

   loop = uv_default_loop();

   /* Create upd server socket, and process data.
    */
   uv_udp_init(loop, &recv_socket);
   struct sockaddr_in recv_addr = uv_ip4_addr("0.0.0.0", localport);
   uv_udp_bind(&recv_socket, recv_addr, 0);
   recv_socket.data = 0;
   uv_udp_recv_start(&recv_socket, alloc_buffer, udp_recv);

   return uv_run(loop, UV_RUN_DEFAULT);
}
