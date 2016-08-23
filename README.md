For <a href="http://blog.apk.li/2012/05/17/bytestream-over-udp-for-mobile.html">bytestream over udp for mobile</a>.

## Status:

Flaky. Probably unsuited for fat pipes, although I ran youtube musik
(i.e. no moving images) through it.

## Build:

    sh prep.sh # once, gets and builds libuv
    make

## Elementary use:

Server: `exe/udp_srv`. It will open an UDP socket on port 9020.

Client: `exe/udp_clt a127.0.0.1 l`, replacing IP address of real server.
Connecting here on port 2222 will connect to the server side's sshd.
