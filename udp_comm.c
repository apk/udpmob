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
   unsigned char data [1];
} data_t;

data_t *data_make (unsigned char *data, int len) {
   data_t *d = malloc (sizeof (data_t) + len);
   if (len) memcpy (d->data, data, len);
   d->len = len;
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

   int flags;
#define FL_IEOF 1 /* EOF on tcp->udp */
#define FL_OEOF 2 /* EOF on udp->tcp */
#define FL_ISDEAD(x) ((x) & (FL_IEOF | FL_OEOF) == (FL_IEOF | FL_OEOF))
   int open; /* Whether tcpsock exists. */
   uv_tcp_t tcpsock;
   struct sockaddr_in addr; // Peer addr (current one for server)
#ifdef CLT
   uv_timer_t timer;
   int pcnt;
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
      if (i == 48) {
         fprintf (stderr, "...+%d\n", n - i);
         return;
      }
      if (i && !(i % 16)) {
         fprintf (stderr, "\n%s:%3d:", name, i);
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

void peer_send_data (peer_t *p) {
   /* Send an data frame now (the first one, or crash if none). */
   sbuf_t S;
   unsigned char *d = sbuf_init (&S, 12 + p->sndlist->len);
   fprintf (stderr, "peer_send_data(%d,%d,%d,%d)\n",
            p->id, p->ack, p->seq, p->sndlist->len);
   putint (&d, p->id, 3);
   *d ++ = 0;
   putint (&d, p->ack, 4);
   putint (&d, p->seq, 4);
   memcpy (d, p->sndlist->data, p->sndlist->len);
   sbuf_send (&S, p);
}

void peer_send_something (peer_t *p, int do_ack) {
   if (p->id == -1) return;
   if (p->sndlist) {
      peer_send_data (p);
   } else if (do_ack) {
      peer_send_ack (p);
#ifdef CLT
      p->pcnt = 20;
#endif
   }
}

#ifdef CLT
void fire (uv_timer_t* handle, int status) {
   peer_t *p = handle->data;
   if (status) {
      fprintf (stderr, "fire %d\n", status);
   }
   if (p->pcnt > 0) p->pcnt --;
   if (p->id == -1) {
      peer_send_req (p);
   } else {
      peer_send_something (p, p->pcnt < 1);
   }
   uv_timer_start (&p->timer, fire, 1000, 0);
}
#endif

void peer_start (peer_t *p, struct sockaddr_in ad, int id, int havetcp);
void peer_kill (peer_t *p, const char *why);
void peer_open (peer_t *p);

#ifndef CLT
void on_connect (uv_connect_t *req, int status) {
   peer_t *p = req->data;
   fprintf (stderr, "on_connect %d\n", status);
   if (status) {
      peer_kill (p, "connect fail");
   } else {
      peer_open (p);
   }
   free (req);
}
#endif

void on_write (uv_write_t *req, int status) {
   peer_t *p = req->data;
   fprintf (stderr, "on_write %d\n", status);
   if (status) {
      peer_kill (p, "write fail");
   } else {
      /* Well, ok. */
      /* XXX Except that we probably have to free a buffer? */
   }
   free (req);
}

void on_shutdown (uv_shutdown_t *req, int status) {
   peer_t *p = req->data;
   fprintf (stderr, "on_shutdown %d\n", status);
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
            p = malloc (sizeof (peer_t));
            peer_start (p, *addr, nid, 0);
            p->next = peerlist;
            peerlist = p;
            /* Initiate outgoing connection. */
            p->open == -1;
            uv_tcp_init (loop, &p->tcpsock);
            req->data = p;
            uv_tcp_connect (req, &p->tcpsock,
                            uv_ip4_addr ("127.0.0.1", 22),
                            on_connect);
         }
         peer_send_ack (p);
      }
#endif
      return;
   } else if (len == 8 || len >= 12) {
      int sendack = 0;
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
      if (len >= 12) {
         /* Have data or EOF */
         fprintf (stderr, "pck=%d ack=%d open=%d len=%d\n",
                  pck, p->ack, p->open, len);
         if (p->ack > pck) {
            /* Peer sends old packets, get him updated. */
            sendack = 1;
         }
         if (pck == p->ack && p->open == 1) {
            /* Packet is in sequence (otherwise we ignore it),
             * and output is open (otherwise we also rely on
             * retransmission.
             */
            if (len == 12) {
               /* EOF */
               uv_shutdown_t *req = malloc (sizeof (uv_shutdown_t));
               uv_shutdown (req, (uv_stream_t*)&p->tcpsock, on_shutdown);
               p->flags |= FL_OEOF;
            } else {
               /* Data */
               uv_write_t *req = malloc (sizeof (uv_write_t));
               unsigned char *dp = malloc (len - 12);
               uv_buf_t *buf = malloc (sizeof (uv_buf_t));
               memcpy (dp, data + 12, len - 12);
               buf [0] = uv_buf_init ((char *)dp, len - 12);
               uv_write (req, (uv_stream_t*)&p->tcpsock, buf, 1, on_write);

            }
            p->ack ++;
         }
      }
      peer_send_something (p, sendack);

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
      fprintf (stderr, "Recv from %s:%d\n", sender,
               ntohs (((struct sockaddr_in *) addr)->sin_port));
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

void tcp_read (uv_stream_t *str, ssize_t nread, uv_buf_t buf) {
   data_t *d, **pp;
   peer_t *p = str->data;
   fprintf (stderr, "** tcp_read: %d\n", nread);
   if (nread == 0) {
      return;
   }
   if (nread == UV_EOF) {
      fprintf(stderr, "EOF on peer...\n");
      nread = 0; /* Hackyflag. */
   } else if (nread < 0) {
      uv_err_t e = uv_last_error (loop);
      if (e.code == UV_EOF) {
         /* XXX Urks, EOF yields nread==-1 and last_error == EOF :-( */
         nread = 0; /* Also hack. */
      } else {
         fprintf(stderr, "Read error %s\n", uv_err_name(e));
         peer_kill(p, "dead tcp");
         return;
      }
   }
   for (pp = &p->sndlist; *pp; pp = &(*pp)->next);
   if (nread < 500) {
      d = data_make ((unsigned char *)buf.base, nread);
      d->next = *pp;
      *pp = d;
   } else {
      int p = 0;
      while (p < nread) {
         int s = nread - p;
         d = data_make (p + (unsigned char *)buf.base, s);
         p += s;
         d->next = *pp;
         *pp = d;
         pp = &d->next;
      }
   }
   peer_send_something (p, 0);
   if (nread == 0) p->flags |= FL_IEOF;
}

void peer_start (peer_t *p, struct sockaddr_in ad, int id, int havetcp) {
   // Assume tcpsock already set up (non-reading)
#ifdef CLT
   uv_timer_init (loop, &p->timer);
   p->timer.data = p;
   p->pcnt = 0;
   uv_timer_start (&p->timer, fire, 1000, 0);
#endif
   p->addr = ad;
   p->id = id;
   p->flags = 0;
   p->open = 0;
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
