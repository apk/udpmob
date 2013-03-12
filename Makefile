all : exe/udp_clt exe/udp_srv

exe/udp_srv : exe/obj/udp_srv.o
	mkdir -p exe
	cc -Bstatic -o exe/udp_srv exe/obj/udp_srv.o ../libuv/libuv.a -framework Foundation \
           -framework CoreServices \
           -framework ApplicationServices \

exe/obj/udp_srv.o : udp_srv.c udp_comm.c
	mkdir -p exe/obj
	cc -I../libuv/include -c -g -o exe/obj/udp_srv.o -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_DARWIN_USE_64_BIT_INODE=1 udp_srv.c

exe/udp_clt : exe/obj/udp_clt.o
	mkdir -p exe
	cc -Bstatic -o exe/udp_clt exe/obj/udp_clt.o ../libuv/libuv.a -framework Foundation \
           -framework CoreServices \
           -framework ApplicationServices \

exe/obj/udp_clt.o : udp_clt.c udp_comm.c
	mkdir -p exe/obj
	cc -I../libuv/include -c -g -o exe/obj/udp_clt.o -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_DARWIN_USE_64_BIT_INODE=1 udp_clt.c
