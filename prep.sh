cd ..
if test -d libuv; then
  :
else
  git clone https://github.com/libuv/libuv || exit 1
fi
p="`pwd`"
cd libuv || exit 1
sh autogen.sh || exit 1
./configure --prefix="$p"/localuv || exit 1
make || exit 1
make install || exit 1
