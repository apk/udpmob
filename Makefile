all : exe/udp_clt exe/udp_srv

CFLG=-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 \
     -D_GNU_SOURCE -D_DARWIN_USE_64_BIT_INODE=1
LFLG= ../libuv/libuv.a -framework Foundation \
      -framework CoreServices -framework ApplicationServices

exe/udp_srv : exe/obj/udp_srv.o
	@mkdir -p exe
	@echo LN SRV
	@cc -Bstatic -o exe/udp_srv exe/obj/udp_srv.o $(LFLG)

exe/obj/udp_srv.o : udp_srv.c udp_comm.c
	@mkdir -p exe/obj
	@echo CC SRV
	@cc -I../libuv/include -c -g -o exe/obj/udp_srv.o $(CFLG) udp_srv.c

exe/udp_clt : exe/obj/udp_clt.o
	@mkdir -p exe
	@echo LN CLT
	@cc -Bstatic -o exe/udp_clt exe/obj/udp_clt.o $(LFLG)

exe/obj/udp_clt.o : udp_clt.c udp_comm.c
	@mkdir -p exe/obj
	@echo CC CLT
	@cc -I../libuv/include -c -g -o exe/obj/udp_clt.o $(CFLG) udp_clt.c

clean ::
	git clean -fdX
