# hack around phpize's bug handling newer libtools

phpize
aclocal
libtoolize --force
autoheader
autoconf
./configure --enable-phpllvm --with-php-source=/home/nuno/php-src
