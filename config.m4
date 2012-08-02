PHP_ARG_ENABLE(phpllvm, whether to enable LLVM based compiler,
[  --enable-phpllvm        Enable LLVM based execution])

PHP_ARG_WITH(llvm-bins, location of the LLVM binaries,
[  --with-llvm-bins=DIR    Location of the LLVM binaries], no, no)

PHP_ARG_WITH(php-source, location of the PHP source code,
[  --with-php-source=DIR   Location of the PHP source code], no, no)

if test "$PHP_PHPLLVM" = "yes"; then
  AC_DEFINE(HAVE_PHPLLVM, 1, [Whether you have LLVM based execution enabled])

  phpllvm_sources="phpllvm.cpp phpllvm_execute.cpp phpllvm_compile.cpp"

  llvm_bins_path="";
  if test "$PHP_LLVM_BINS" != "no"; then
      if test "$PHP_LLVM_BINS" = "yes"; then
        AC_MSG_ERROR([You must specify a path when using --with-llvm])
      fi
      llvm_bins_path="$PHP_LLVM_BINS/";
  fi
  PHP_SUBST(llvm_bins_path)

  AC_PATH_PROG(PROG_CLANG, clang,[], $llvm_bins_path:$PATH)
  if test "$PROG_CLANG" != ""; then
    LLVM_CC=$PROG_CLANG
  else
    AC_MSG_ERROR([Clang was not found])
  fi
  PHP_SUBST(LLVM_CC)

  if test "$PHP_PHP_SOURCE" = "no" -o "$PHP_PHP_SOURCE" = "yes"; then
    AC_MSG_ERROR([You must specify --with-php-source=DIR when using --enable-phpllvm])
  fi
  php_sources_path="$PHP_PHP_SOURCE";
  PHP_SUBST(php_sources_path)

  dnl Link LLVM libraries:
  LLVM_LDFLAGS=`${llvm_bins_path}llvm-config --ldflags --libs core jit native bitwriter bitreader scalaropts ipo target analysis executionengine support`
  LLVM_CXXFLAGS=`${llvm_bins_path}llvm-config --cxxflags`
  LDFLAGS="$LDFLAGS $LLVM_LDFLAGS"
  CXXFLAGS="$CXXFLAGS $LLVM_CXXFLAGS"

  PHP_REQUIRE_CXX()
  PHP_NEW_EXTENSION(phpllvm, $phpllvm_sources, $ext_shared,,, 1)

  PHP_ADD_MAKEFILE_FRAGMENT
fi
