// -*- mode: C; c-basic-offset: 3; tab-width: 8; indent-tabs-mode: nil -*-

#include <stdio.h>
#include <stdlib.h>

#define AT (fprintf (stderr, "__%s:%d__\n", __FILE__, __LINE__))

#include "uv.h"

uv_loop_t *loop;

uv_buf_t alloc_buffer(uv_handle_t *handle, size_t suggested_size) {
   return uv_buf_init((char*) malloc(suggested_size), suggested_size);
}

#ifndef CLT
uv_udp_t recv_socket;
#endif

typedef struct data {
   struct data *next;
   int len;
   unsigned char buf [1];
} data_t;

void data_drop (data_t *d) {
   free (d);
}

typedef struct peer {
#ifndef CLT
   struct peer *next;
#endif
   int id;
   int ack; /* Ack to send. */
   int seq; /* Sequence number of first packet in send queue. */
   data_t *sndlist; /* Enqueued data. */

   uv_tcp_t tcpsock;
   struct sockaddr_in addr; // Peer addr (current one for server)
#ifdef CLT
   uv_timer_t timer;
#endif
#ifdef CLT
   uv_udp_t udp;
#endif
} peer_t;

#ifndef CLT
peer_t *peerlist = 0;
#endif

void dumpbuf(const char *name, uv_buf_t buf, int n) {
   int i;
   fprintf (stderr, "%s:   :", name);
   for (i = 0; i < n; i ++) {
      if (i && !(i % 16)) {
         fprintf (stderr, "\n%s:%3d:", name);
      }
      fprintf (stderr, " %02x", buf.base [i] & 255);
   }
   fprintf (stderr, "\n");
}

void on_send (uv_udp_send_t* req, int status) {
   if (status) {
      fprintf (stderr, "on_send %d\n", status);
   }
   /* XXX Is there anything we might need to free? */
}

typedef struct sbuf {
   uv_buf_t *buf;
   int sz; /* XXX Probably superfluous, and then the whole struct. */
} sbuf_t;

unsigned char *sbuf_init (sbuf_t *sb, int len) {
   sb->buf = malloc (sizeof (uv_buf_t));
   sb->sz = len;
   sb->buf [0] = uv_buf_init (malloc (len), len);
   return (unsigned char *)sb->buf->base;
}

void sbuf_send (sbuf_t *sb, peer_t *p) {
   uv_udp_send_t *req = malloc (sizeof (uv_udp_send_t));
   char sender[17] = { 0 };
   uv_ip4_name(&p->addr, sender, 16);
   fprintf (stderr, "Send to %s:%d\n", sender, ntohs (p->addr.sin_port));
   dumpbuf (" =>", *sb->buf, sb->sz);
   uv_udp_send (req,
#ifdef CLT
                &p->udp,
#else
                &recv_socket,
#endif
                sb->buf, 1, p->addr, on_send);
}

void putint (unsigned char **dp, int val, int len) {
   unsigned char *d = *dp;
   *dp += len;
   while (len > 0) {
      len --;
      d [len] = val;
      val >>= 8; /* Sign extension will not matter. */
   }
}

void peer_send_req (peer_t *p) {
   /* Send an initial request frame. */
   sbuf_t S;
   unsigned char *d = sbuf_init (&S, 1);
   fprintf (stderr, "peer_send_req\n");
   d [0] = 0;
   sbuf_send (&S, p);
}

void peer_send_ack (peer_t *p) {
   /* Send an ack-only frame now. */
   sbuf_t S;
   unsigned char *d = sbuf_init (&S, 8);
   fprintf (stderr, "peer_send_ack(%d,%d)\n", p->id, p->ack);
   putint (&d, p->id, 3);
   *d ++ = 0;
   putint (&d, p->ack, 4);
   sbuf_send (&S, p);
}

#ifdef CLT
void fire (uv_timer_t* handle, int status) {
   peer_t *p = handle->data;
   fprintf (stderr, "fire %d\n", status);
   if (p->id == -1) {
      peer_send_req (p);
   } else {
      peer_send_ack (p);
   }
   uv_timer_start (&p->timer, fire, 3000, 0);
}
#endif

void peer_start (peer_t *p, struct sockaddr_in ad, int id);
void peer_kill (peer_t *p, const char *why);

int getint (unsigned char *data, int len, int off, int cnt) {
   int v = 0;
   int idx = off;
   while (cnt > 0) {
      v = (v << 8) | (idx < len ? data [idx] & 255 : 0);
      cnt --;
      idx ++;
   }
   return v;
}

int sameaddr (struct sockaddr_in *a, struct sockaddr_in *b) {
   return (a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port);
}

void process (peer_t *p, char *d, int len, uv_udp_t *io,
              struct sockaddr_in *addr)
{
   unsigned char *data = (unsigned char *)d;
   int id = getint (data, len, 0, 3);
   int flg = getint (data, len, 3, 1);
   int ack = getint (data, len, 4, 4);
   int pck = getint (data, len, 8, 4);
   fprintf (stderr, "Process(%d)...%d/%x/%d/%d\n", len, id, flg, ack, pck);
   if (p) {
      fprintf (stderr, "          %d/%d/%d\n", p->id, p->seq, p->ack);
   }
   if (len < 4) {
      /* Initial frame; contents are actually irrelevant. */
#ifndef CLT
      if (!p) {
         /* We can only accept new connections server-side. */

         /* We need to look into the peer list if there is currently one
          * for this remote address. Otherwise we'd create one for each
          * retransmitted connection request.
          */
         for (p = peerlist; p; p = p->next) {
            if (sameaddr (&p->addr, addr)) break;
         }
         if (!p) {
            int nid = time (0) ^ getpid ();
            p = peerlist;
            while (1) {
               nid &= 0x7fffff; /* This is always done at least once. */
               if (!p) break;
               if (p->id == nid) {
                  nid --;
                  p = peerlist;
               } else {
                  p = p->next;
               }
            }
            p = malloc (sizeof (peer_t));
            peer_start (p, *addr, nid);
            p->next = peerlist;
            peerlist = p;
         }
         peer_send_ack (p);
      }
#endif
      return;
   } else if (len == 8 || len >= 12) {
      if (!p) {
#ifndef CLT
         /* Server-side: We need to find the peer talking to us. */
         for (p = peerlist; p; p = p->next) {
            if (p->id == id) break;
         }
         if (!p) {
            fprintf (stderr, "Ignoring unknown peer %d\n", id);
            return;
         }
#else
         fprintf (stderr, "No peer?\n");
         return;
#endif
      }
      /* Ok, now we know a peer to work on. */
#ifdef CLT
      if (p->id == -1) {
         AT;
         /* Pick up our assigned id if we don't have one yet. */
         p->id = id;
      } else if (p->id != id) {
         /* Hey, what's that? */
         peer_kill (p, "id mismatch");
         return;
      }
#endif
      /* Process acks first: We can drop packets that we got ack'd. */
      while (p->seq < ack) {
         data_t *d = p->sndlist;
         if (!d) {
            peer_kill (p, "ack ahead of snd");
            return;
         }
         p->sndlist = d->next;
         data_drop (d);
         p->seq ++;
      }

      /* Regular frame:
       * 3 bytes: Connection identity
       * 1 byte: Flags
       * 4 bytes: Acknowledgement number
       * 4 bytes: Packet number
       * n bytes: Data
       *
       * 8 byte size means ack-only packet; 12 byte size is an
       * empty data packet which means EOF on the connection.
       * Data packets need to contain at least one byte of data.
       */
      fprintf (stderr, "Regular frame\n");
   }
}

void udp_recv (uv_udp_t *req, ssize_t nread, uv_buf_t buf,
               struct sockaddr *addr, unsigned flags)
{
   if (nread == -1) {
      fprintf(stderr, "Read error %s\n", uv_err_name(uv_last_error(loop)));
      uv_close((uv_handle_t*) req, NULL);
      free(buf.base);
      return;
   }

   if (addr) {
      char sender[17] = { 0 };
      uv_ip4_name((struct sockaddr_in*) addr, sender, 16);
      fprintf (stderr, "Recv from %s\n", sender);
   } else {
#if 0
      fprintf(stderr, "Recv from unknown\n");
#endif
      free(buf.base);
      return;
   }

   dumpbuf (" <=", buf, nread);

   process (req->data, buf.base, nread, req, (struct sockaddr_in *)addr);

   free (buf.base);
}

void peer_start (peer_t *p, struct sockaddr_in ad, int id) {
   // Assume tcpsock already set up (non-reading)
#ifdef CLT
   uv_timer_init (loop, &p->timer);
   p->timer.data = p;
   uv_timer_start (&p->timer, fire, 1000, 0);
#endif
   p->addr = ad;
   p->id = id;
   {
      char sender[17] = { 0 };
      uv_ip4_name(&ad, sender, 16);
      fprintf (stderr, "peer_start(%d:%s:%d)\n", p->id,
               sender, ntohs (ad.sin_port));
   }

   p->ack = 0;
   p->seq = 0;
   p->sndlist = 0;

#ifdef CLT
   uv_udp_init (loop, &p->udp);
   p->udp.data = p;
   uv_udp_bind (&p->udp, uv_ip4_addr("0.0.0.0", 0), 0);
   uv_udp_recv_start (&p->udp, alloc_buffer, udp_recv);
#endif
}

void peer_kill (peer_t *p, const char *why) {
   char sender[17] = { 0 };
   uv_ip4_name(&p->addr, sender, 16);
   fprintf (stderr, "peer_kill(%d:%s:%d): %s\n",
            p->id, sender, ntohs (p->addr.sin_port), why);
   exit (1);
}
