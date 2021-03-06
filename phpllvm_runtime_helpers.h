/*
   +----------------------------------------------------------------------+
   | PHP LLVM extension                                                   |
   +----------------------------------------------------------------------+
   | Copyright (c) 2008-2012 The PHP Group                                |
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

#ifndef PHPLLVM_RUNTIME_HELPERS_H
#define PHPLLVM_RUNTIME_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "php.h"

typedef void (zend_execute_t)(zend_op_array *op_array TSRMLS_DC);

/* A helper type used to store the data usually stored on the zend_execute function's
 	stack. It's just more convinient to wrap it up in one struct. */
typedef struct _execute_stack_data execute_stack_data;

zval ** phpllvm_get_exception_pp(TSRMLS_D);

void phpllvm_init_executor(execute_stack_data * stack_data, zend_op_array *op_array TSRMLS_DC);

void phpllvm_create_execute_data(execute_stack_data *stack_data TSRMLS_DC);

zend_execute_data *phpllvm_get_execute_data(execute_stack_data *stack_data);

void phpllvm_pre_vm_return(execute_stack_data *stack_data TSRMLS_DC);

#ifdef DEBUG_PHPLLVM
void phpllvm_verify_opline(execute_stack_data *stack_data, int i);
#endif

int phpllvm_get_opline_number(execute_stack_data *stack_data);

opcode_handler_t phpllvm_get_opcode_handler(zend_op* op);

#ifdef __cplusplus
}
#endif

#endif
