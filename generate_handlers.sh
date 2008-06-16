#!/bin/bash

#   +----------------------------------------------------------------------+
#   | PHP LLVM extension                                                   |
#   +----------------------------------------------------------------------+
#   | Copyright (c) 2008 The PHP Group                                     |
#   +----------------------------------------------------------------------+
#   | This source file is subject to version 3.01 of the PHP license,      |
#   | that is bundled with this package in the file LICENSE, and is        |
#   | available through the world-wide-web at the following url:           |
#   | http://www.php.net/license/3_01.txt                                  |
#   | If you did not receive a copy of the PHP license and are unable to   |
#   | obtain it through the world-wide-web, please send a note to          |
#   | license@php.net so we can mail you a copy immediately.               |
#   +----------------------------------------------------------------------+
#   | Author: Joonas Govenius <joonas@php.net>                             |
#   +----------------------------------------------------------------------+

LLVM_CC="llvm-gcc"
LLVM_LINK="llvm-link"
PHP_INCLUDES="/usr/local/include/php"
PHP_SOURCES="../php5"

INCLUDES="-I. -I$PHP_INCLUDES -I$PHP_INCLUDES/main -I$PHP_INCLUDES/TSRM -I$PHP_INCLUDES/Zend -I$PHP_INCLUDES/ext -I$PHP_INCLUDES/ext/date/lib"
OPTS="-DPHP_ATOM_INC  -DHAVE_CONFIG_H -g -O2 -fno-common -DPIC -emit-llvm"

$LLVM_CC $OPTS $INCLUDES -c $PHP_SOURCES/Zend/zend_exceptions.c -o exceptions.bc.o
$LLVM_CC $OPTS $INCLUDES -c $PHP_SOURCES/Zend/zend_execute.c -o handlers.bc.o
$LLVM_CC $OPTS $INCLUDES -c phpllvm_runtime_helpers.c -o phpllvm_runtime_helpers.bc.o
$LLVM_LINK phpllvm_runtime_helpers.bc.o handlers.bc.o exceptions.bc.o > module_template.bc
