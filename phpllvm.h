/*
   +----------------------------------------------------------------------+
   | PHP LLVM extension                                                   |
   +----------------------------------------------------------------------+
   | Copyright (c) 2008 The PHP Group                                     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Joonas Govenius <joonas@php.net>                             |
   +----------------------------------------------------------------------+
*/

#ifndef PHP_PHPLLVM_H
#define PHP_PHPLLVM_H 1

#ifdef ZTS
#include "TSRM.h"
#endif

ZEND_BEGIN_MODULE_GLOBALS(phpllvm)
    zend_execute_data *execute_data;
ZEND_END_MODULE_GLOBALS(phpllvm)

#ifdef ZTS
#define PHPLLVM_G(v) TSRMG(phpllvm_globals_id, zend_phpllvm_globals *, v)
#else
#define PHPLLVM_G(v) (phpllvm_globals.v)
#endif

#define PHP_PHPLLVM_VERSION "0.1"
#define PHP_PHPLLVM_EXTNAME "phpllvm"

extern zend_module_entry phpllvm_module_entry;
#define phpext_phpllvm_ptr &phpllvm_module_entry

#endif
