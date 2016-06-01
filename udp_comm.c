// -*- mode: C; c-basic-offset: 3; tab-width: 8; indent-tabs-mode: nil -*-

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AT (fprintf (stderr, "__%s:%d__\n", __FILE__, __LINE__))

#include "uv.h"

static int debug = 0;

uv_loop_t *loop;

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
   *buf = uv_buf_init((char*) malloc(suggested_size), suggested_size);
}

uint64_t now () {
   return uv_now (loop);
}

#ifndef CLT
uv_udp_t recv_socket;
#endif

typedef struct data {
   struct data *next;
   int len;
   int ena;
   uint64_t point;
   unsigned char data [1];
} data_t;

data_t *data_make (unsigned char *data, int len) {
   data_t *d = malloc (sizeof (data_t) + len);
   if (len) memcpy (d->data, data, len);
   d->len = len;
   d->point = now ();
   d->ena = 1;
   return d;
}

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

   uint64_t next_ack;
   uint64_t last_recv;

   int sel; /* Output selector, client only */
   int flags;
#define FL_IEOF 1 /* EOF on tcp->udp */
#define FL_OEOF 2 /* EOF on udp->tcp */
#define FL_ISDEAD(x) ((x) & (FL_IEOF | FL_OEOF) == (FL_IEOF | FL_OEOF))
   int open; /* Whether tcpsock exists. */
   uv_tcp_t tcpsock;
   struct sockaddr_in addr; // Peer addr (current one for server)
   uv_timer_t timer;
#ifdef CLT
   uv_udp_t udp;
#endif
} peer_t;

#ifndef CLT
peer_t *peerlist = 0;
#endif

void dumpbuf(const char *name, const uv_buf_t *buf, int n) {
   int i;
   fprintf (stderr, "%s:   :", name);
   for (i = 0; i < n; i ++) {
      if (i == 48) {
         fprintf (stderr, "...+%d\n", n - i);
         return;
      }
      if (i && !(i % 16)) {
         fprintf (stderr, "\n%s:%3d:", name, i);
      }
      fprintf (stderr, " %02x", buf->base [i] & 255);
   }
   fprintf (stderr, "\n");
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

struct send_data {
   uv_udp_send_t req;
   uv_buf_t *buf;
};

void on_send (uv_udp_send_t* req, int status) {
   if (status) {
      fprintf (stderr, "on_send %d\n", status);
   }
   /* XXX Is there anything we might need to free? */
   free (((struct send_data *)req->data)->buf->base);
   free (((struct send_data *)req->data)->buf);
   free (req->data);
}

void sbuf_send (sbuf_t *sb, peer_t *p) {
   struct send_data *d = malloc (sizeof (struct send_data));
   char sender[17] = { 0 };
   uv_ip4_name(&p->addr, sender, 16);
   if (debug > 2) {
      fprintf (stderr, "Send to %s:%d\n", sender, ntohs (p->addr.sin_port));
      dumpbuf (" =>", sb->buf, sb->sz);
   }
   d->buf = sb->buf;
   d->req.data = d;
   uv_udp_send (&d->req,
#ifdef CLT
                &p->udp,
#else
                &recv_socket,
#endif
                sb->buf, 1, (struct sockaddr *)&p->addr, on_send);
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

void peer_set_timer (peer_t *p);

void peer_send_req (peer_t *p) {
   /* Send an initial request frame. */
   sbuf_t S;
   unsigned char *d = sbuf_init (&S, 1);
   d [0] = p->sel;
   sbuf_send (&S, p);
   if (debug > 0) {
      fprintf (stderr, "peer_send_req()\n");
   }
   /* Abuse the ack timer for our resends */
   p->next_ack = now () + 1200;
   peer_set_timer (p);
}

void peer_send_ack (peer_t *p) {
   /* Send an ack-only frame now. */
   sbuf_t S;
   unsigned char *d = sbuf_init (&S, 8);
   if (debug > 0) {
      fprintf (stderr, "peer_send_ack(%d,%d)\n", p->id, p->ack);
   }
   putint (&d, p->id, 3);
   *d ++ = 0;
   putint (&d, p->ack, 4);
   sbuf_send (&S, p);
   p->next_ack = now () + 1200;
   peer_set_timer (p);
}

int peer_send_data (peer_t *p) {
   sbuf_t S;
   data_t *e;
   int n = 0;
   for (e = p->sndlist; e && n < 9; e = e->next, n ++) {
      if (e->ena && e->point <= now ()) {
         unsigned char *d = sbuf_init (&S, 12 + e->len);
         if (debug > 0) {
            fprintf (stderr, "peer_send_data(%d,%d,%d,%d)\n",
                     p->id, p->ack, p->seq + n, e->len);
         }
         putint (&d, p->id, 3);
         *d ++ = 0;
         putint (&d, p->ack, 4);
         putint (&d, p->seq + n, 4);
         memcpy (d, e->data, e->len);
         sbuf_send (&S, p);
         e->point = now () + 800;
         e->ena = 0;
         return 1;
      }
   }
   return 0;
}

void fire (uv_timer_t* handle);

void peer_send_something (peer_t *p) {
   uint64_t n = now ();
   if (p->id == -1) return;
#ifndef CLT
   if (p->last_recv + 30000 < now ()) {
      /* If we as a server didn't hear anything from
       * the other side we're not going to send anything
       * until we hear back. We don't want to sprinkle
       * an IP that may not even belong to the client
       * anymore. We can also slow down the timer,
       * any new receive will restart it.
       */
      uv_timer_start (&p->timer, fire, 1500, 0);
      return;
   }
#endif
   {
      if (peer_send_data (p)) {
         /* nothing more, is in condition... */
      } else if (p->next_ack <= n) {
         peer_send_ack (p);
      }
   }
   peer_set_timer (p);
}

void fire (uv_timer_t* handle) {
   peer_t *p = handle->data;
   if (p->id == -1) {
      peer_send_req (p);
   } else {
      peer_send_something (p);
   }
   uv_timer_start (&p->timer, fire, 1000, 0);
}

void peer_set_timer (peer_t *p) {
   uint64_t k = p->next_ack;
   data_t *e = p->sndlist;
   if (e && e->ena && e->point < k) {
      k = e->point;
   }
   k -= now ();
   if (k < 200) k = 200;
   uv_timer_start (&p->timer, fire, 1000, 0);
}

void peer_start (peer_t *p, const struct sockaddr_in *ad, int id, int havetcp);
void peer_kill (peer_t *p, const char *why);
void peer_open (peer_t *p);

#ifndef CLT
void on_connect (uv_connect_t *req, int status) {
   peer_t *p = req->data;
   if (status) {
      fprintf (stderr, "on_connect %d\n", status);
   }
   if (status) {
      peer_kill (p, "connect fail");
   } else {
      peer_open (p);
   }
   free (req);
}
#endif

struct write_data {
   uv_write_t req;
   uv_buf_t *buf;
};

void on_write (uv_write_t *req, int status) {
   peer_t *p = req->data;
   if (status) {
      fprintf (stderr, "on_write %d\n", status);
   }
   if (status) {
      peer_kill (p, "write fail");
   } else {
      /* Well, ok. */
      /* XXX Except that we probably have to free a buffer? */
   }
   free (((struct write_data *)req->data)->buf->base);
   free (((struct write_data *)req->data)->buf);
   free (req->data);
}

void on_shutdown (uv_shutdown_t *req, int status) {
   peer_t *p = req->data;
   if (status) {
      fprintf (stderr, "on_shutdown %d\n", status);
   }
   if (status) {
      peer_kill (p, "shutdown fail");
   } else {
      /* Well, ok. */
      /* XXX Except that we probably have to free a buffer? */
   }
   free (req);
}

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
   if (debug > 0) {
      fprintf (stderr, "Process(%d)...%d/%x/%d/%d\n", len, id, flg, ack, pck);
      if (p) {
         fprintf (stderr, "          %d/%d/%d\n", p->id, p->seq, p->ack);
      }
   }
   if (len < 4) {
      /* Initial frame; contents are actually irrelevant (yet). */
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
            struct sockaddr_in sin;
            struct reminfo *remp =
               rems + (len > 0 && data [0] < nrem ? data [0] : 0);
            char oa[17] = { 0 };
            uv_connect_t *req = malloc (sizeof (uv_connect_t));
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
            uv_ip4_name(addr, oa, 16);
            fprintf (stderr, "clt %s:%d\n", oa, ntohs (addr->sin_port));
            p = malloc (sizeof (peer_t));
            peer_start (p, addr, nid, 0);
            p->next = peerlist;
            peerlist = p;
            /* Initiate outgoing connection. */
            p->open == -1;
            uv_tcp_init (loop, &p->tcpsock);
            req->data = p;
            uv_ip4_addr (remp->remaddr, remp->remport, &sin);
            uv_tcp_connect (req, &p->tcpsock,
                            (struct sockaddr *)&sin,
                            on_connect);
         }
         peer_send_ack (p);
      }
      p->last_recv = now ();
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
            char oa[17] = { 0 };
            uv_ip4_name(addr, oa, 16);
            fprintf (stderr, "Ignoring unknown peer %d from %s:%d\n",
                     id, oa, ntohs (addr->sin_port));
            return;
         }
         if (!sameaddr (&p->addr, addr)) {
            char oa[17] = { 0 };
            uv_ip4_name(&p->addr, oa, 16);
            char na[17] = { 0 };
            uv_ip4_name(addr, na, 16);
            fprintf (stderr, "clt %s:%d => %s:%d\n",
                     oa, ntohs (p->addr.sin_port),
                     na, ntohs (addr->sin_port));
            p->addr = *addr;
         }
#else
         fprintf (stderr, "No peer?\n");
         return;
#endif
      }
      /* Ok, now we know a peer to work on. */
      p->last_recv = now ();
#ifdef CLT
      if (p->id == -1) {
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
         p->next_ack = now ();
      }
      if (len >= 12) {
         /* Have data or EOF */
         if (debug > 1) {
            fprintf (stderr, "pck=%d ack=%d open=%d len=%d\n",
                     pck, p->ack, p->open, len);
         }
         if (p->ack > pck) {
            /* Peer sent old packet, get him updated. */
            p->next_ack = now ();
         }
         if (pck == p->ack && p->open == 1) {
            /* Packet is in sequence (otherwise we ignore it),
             * and output is open (otherwise we also rely on
             * retransmission).
             */
            if (len == 12) {
               /* EOF */
               uv_shutdown_t *req = malloc (sizeof (uv_shutdown_t));
               uv_shutdown (req, (uv_stream_t*)&p->tcpsock, on_shutdown);
               p->flags |= FL_OEOF;
            } else {
               /* Data */
               struct write_data *d = malloc (sizeof (struct write_data));
               unsigned char *dp = malloc (len - 12);
               d->buf = malloc (sizeof (uv_buf_t));
               memcpy (dp, data + 12, len - 12);
               d->buf [0] = uv_buf_init ((char *)dp, len - 12);
               d->req.data = d;
               uv_write (&d->req, (uv_stream_t*)&p->tcpsock, d->buf, 1, on_write);

            }
            p->ack ++;
            /* We processed a packet, send the ack immediately. */
            p->next_ack = now ();
         }
      }
      { data_t *d;
         int n = 0;
         for (d = p->sndlist; d && n < 3; d = d->next, n ++) {
            d->ena = 1;
         }
      }
      peer_send_something (p);

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
   }
}

void udp_recv (uv_udp_t *req, ssize_t nread, const uv_buf_t *buf,
               const struct sockaddr *addr, unsigned flags)
{
   if (nread == -1) {
      fprintf(stderr, "Read error %s\n", uv_err_name(nread));
      uv_close((uv_handle_t*) req, NULL);
      free (buf->base);
      return;
   }

   if (addr) {
      char sender[17] = { 0 };
      uv_ip4_name((struct sockaddr_in*) addr, sender, 16);
      if (debug > 2) {
         fprintf (stderr, "Recv from %s:%d\n", sender,
                  ntohs (((struct sockaddr_in *) addr)->sin_port));
         dumpbuf (" <=", buf, nread);
      }
   } else {
#if 0
      fprintf(stderr, "Recv from unknown\n");
#endif
      free (buf->base);
      return;
   }

   process (req->data, buf->base, nread, req, (struct sockaddr_in *)addr);
   free (buf->base);

}

void tcp_read (uv_stream_t *str, ssize_t nread, const uv_buf_t *buf) {
   data_t *d, **pp;
   peer_t *p = str->data;
   if (nread == 0) {
      free (buf->base);
      return;
   }
   if (nread == UV_EOF) {
      fprintf(stderr, "EOF on peer...\n");
      nread = 0; /* Hackyflag. */
   } else if (nread < 0) {
      if (nread == UV_EOF) {
         /* XXX Urks, EOF yields nread==-1 and last_error == EOF :-( */
         nread = 0; /* Also hack. */
      } else {
         fprintf(stderr, "Read error %s\n", uv_err_name(nread));
         peer_kill(p, "dead tcp");
         free (buf->base);
         return;
      }
   }
   for (pp = &p->sndlist; *pp; pp = &(*pp)->next);
   if (nread < 1440) {
      d = data_make ((unsigned char *)buf->base, nread);
      d->next = *pp;
      *pp = d;
      peer_send_something (p);
   } else {
      int pos = 0;
      while (pos < nread) {
         int s = nread - pos;
         if (s > 1400) s = 1400;
         d = data_make (pos + (unsigned char *)buf->base, s);
         pos += s;
         d->next = *pp;
         *pp = d;
         pp = &d->next;
         peer_send_something (p);
      }
   }
   if (nread == 0) p->flags |= FL_IEOF;
   free (buf->base);
}

void peer_start (peer_t *p, const struct sockaddr_in *ad, int id, int havetcp) {
#ifdef CLT
   struct sockaddr_in nulladdr;
#endif
   // Assume tcpsock already set up (non-reading)
   uv_timer_init (loop, &p->timer);
   p->timer.data = p;
   uv_timer_start (&p->timer, fire, 30, 0);

   p->addr = *ad;
   p->id = havetcp ? -1 : id;
   p->sel = id;
   p->flags = 0;
   p->open = 0;
   {
      char sender[17] = { 0 };
      uv_ip4_name(ad, sender, 16);
      fprintf (stderr, "peer_start(%d:%s:%d)\n", p->id,
               sender, ntohs (ad->sin_port));
   }

   p->ack = 0;
   p->seq = 0;
   p->next_ack = now ();
   p->last_recv = now ();
   p->sndlist = 0;

#ifdef CLT
   uv_udp_init (loop, &p->udp);
   p->udp.data = p;
   uv_ip4_addr ("0.0.0.0", 0, &nulladdr);
   uv_udp_bind (&p->udp, (struct sockaddr *)&nulladdr, 0);
   uv_udp_recv_start (&p->udp, alloc_buffer, udp_recv);
#endif
}

void peer_open (peer_t *p) {
   /* Declare tcpsock open */
   if (p->open > 0) {
      fprintf (stderr, "TWO OPEN?\n");
      return;
   }
   p->open = 1;
   p->tcpsock.data = p;
   uv_read_start ((uv_stream_t*)&p->tcpsock, alloc_buffer, tcp_read);
}

void peer_kill (peer_t *p, const char *why) {
   char sender[17] = { 0 };
   uv_ip4_name(&p->addr, sender, 16);
   fprintf (stderr, "peer_kill(%d:%s:%d): %s\n",
            p->id, sender, ntohs (p->addr.sin_port), why);
   /* Be wary of an open uv_tcp_connect()... */
   exit (1);
}
