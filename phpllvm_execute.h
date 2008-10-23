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
   | Authors: Joonas Govenius <joonas@php.net>                            |
   |          Nuno Lopes <nlopess@php.net>                                |
   +----------------------------------------------------------------------+
*/

#ifndef PHPLLVM_EXECUTE_H
#define PHPLLVM_EXECUTE_H

extern "C" {
#include "php.h"
}

namespace phpllvm {

	void init_jit_engine(const char* filename);

	void destroy_jit_engine();

	void override_executor();

	void restore_executor();

	void execute(zend_op_array *op_array TSRMLS_DC);

	void save_module(const char* filename);

}

#endif
