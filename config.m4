PHP_ARG_ENABLE(phpllvm, whether to enable LLVM based execution,
[ --enable-phpllvm   Enable LLVM based execution])

PHP_ARG_WITH(llvm, location of the llvm-config tool,
[  --with-llvm-bins=DIR   Location of the llvm-config tool], no, no)

if test "$PHP_PHPLLVM" == "yes"; then
  AC_DEFINE(HAVE_PHPLLVM, 1, [Whether you have LLVM based execution enabled])

  phpllvm_sources="phpllvm.cpp phpllvm_execute.cpp phpllvm_compile.cpp"
  
  llvm_path="";
  if test "$PHP_LLVM" != "no"; then
      if test "$PHP_LLVM" == "yes"; then
        AC_MSG_ERROR([You must specify a path when using --with-llvm])
      fi

      llvm_path="$PHP_LLVM/";
  fi
  
  dnl Link LLVM libraries:
dnl  LLVM_LDFLAGS=`${llvm_path}llvm-config --ldflags --libs all`
  LLVM_LDFLAGS=`${llvm_path}llvm-config --ldflags --libs core jit native bitwriter bitreader scalaropts ipo target analysis executionengine support`
  LLVM_CXXFLAGS=`${llvm_path}llvm-config --cxxflags`
  LDFLAGS="$LDFLAGS $LLVM_LDFLAGS"
  CXXFLAGS="$CXXFLAGS -g $LLVM_CXXFLAGS"

  PHP_REQUIRE_CXX
  PHP_NEW_EXTENSION(phpllvm, $phpllvm_sources, $ext_shared)
fi
