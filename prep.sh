cd ..
git clone https://github.com/libuv/libuv
p="`pwd`"
cd libuv
sh autogen.sh
./configure --prefix="$p"/localuv
make
make install
