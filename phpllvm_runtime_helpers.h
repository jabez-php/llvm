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

#ifndef LLVMCAHCE_RUNTIME_HELPERS_H
#define LLVMCAHCE_RUNTIME_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "php.h"

typedef void (zend_execute_t)(zend_op_array *op_array TSRMLS_DC);

int phpllvm_executor_exception_exists(TSRMLS_D);

zend_execute_data *phpllvm_create_execute_data(zend_op_array *op_array TSRMLS_DC);

void phpllvm_init_executor(zend_execute_data *execute_data TSRMLS_DC);

void phpllvm_verify_opline(zend_execute_data *execute_data, int i TSRMLS_DC);

int phpllvm_check_opline(zend_execute_data *execute_data, int i TSRMLS_DC);

void phpllvm_set_executor(zend_execute_t new_execute);

opcode_handler_t phpllvm_get_opcode_handler(zend_op* op);

#ifdef __cplusplus
}
#endif

#endif