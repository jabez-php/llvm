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

#include "phpllvm_runtime_helpers.h"
#include <zend_execute.h>

#undef EX
#define EX(element) execute_data->element

int phpllvm_executor_exception_exists(TSRMLS_D) {
	if (EG(exception))
		fprintf(stderr, "Zend engine exception exists.\n");
	return EG(exception) != 0;
}

zend_execute_data *phpllvm_create_execute_data(zend_op_array *op_array TSRMLS_DC) {
	/* has to be allocated on the Zend VM stack because the ZEND_RETURN handler
 		frees arguments to the function by decreasing the Zend VM stack pointer. */
	zend_execute_data *execute_data = (zend_execute_data *)zend_vm_stack_alloc(
		sizeof(zend_execute_data) +
		sizeof(zval**) * op_array->last_var +
		sizeof(temp_variable) * op_array->T TSRMLS_CC);

	EX(CVs) = (zval***)((char*)execute_data + sizeof(zend_execute_data));
	memset(EX(CVs), 0, sizeof(zval**) * op_array->last_var);
	EX(Ts) = (temp_variable *)(EX(CVs) + op_array->last_var);
	EX(fbc) = NULL;
	EX(called_scope) = NULL;
	EX(object) = NULL;
	EX(old_error_reporting) = NULL;
	EX(op_array) = op_array;
	
	return execute_data;
}

void phpllvm_init_executor(zend_execute_data *execute_data TSRMLS_DC) {
	int i;

#if ZEND_MODULE_API_NO < 20071006 /* PHP < 5.3. TODO: remove this later */
	EX(original_in_execution) = EG(in_execution);
#endif
	EX(symbol_table) = EG(active_symbol_table);
	EX(prev_execute_data) = EG(current_execute_data);
	EG(current_execute_data) = execute_data;

	EG(in_execution) = 1;
	if (EX(op_array)->start_op) {
		EX(opline) = EX(op_array)->start_op;
	} else {
		EX(opline) = EX(op_array)->opcodes;
	}

#if ZEND_MODULE_API_NO < 20071006 /* PHP < 5.3. TODO: remove this later */
	if (EX(op_array)->uses_this && EG(This)) {
		Z_ADDREF_P(EG(This)); /* For $this pointer */
		if (zend_hash_add(EG(active_symbol_table), "this", sizeof("this"), &EG(This), sizeof(zval *), NULL)==FAILURE) {
			Z_DELREF_P(EG(This));
		}
	}
#else
	zend_op_array *op_array = EX(op_array);

	if (op_array->this_var != -1 && EG(This)) {
 		Z_ADDREF_P(EG(This)); /* For $this pointer */
		if (!EG(active_symbol_table)) {
			EX(CVs)[op_array->this_var] = (zval**)EX(CVs) + (op_array->last_var + op_array->this_var);
			*EX(CVs)[op_array->this_var] = EG(This);
		} else {
			if (zend_hash_add(EG(active_symbol_table), "this", sizeof("this"), &EG(This), sizeof(zval *), (void**)&EX(CVs)[op_array->this_var])==FAILURE) {
				Z_DELREF_P(EG(This));
			}
		}
	}
#endif

	EG(opline_ptr) = &EX(opline);

	EX(function_state).function = (zend_function *) EX(op_array);
	EX(function_state).arguments = NULL;
}

#define EX_T(offset) (*(temp_variable *)((char *) EX(Ts) + offset))

int phpllvm_check_opline(zend_execute_data *execute_data, int i TSRMLS_DC) {
	if (execute_data->opline == execute_data->op_array->opcodes + i)
		return 1;
	else
		return 0;
}

void phpllvm_verify_opline(zend_execute_data *execute_data, int i TSRMLS_DC) {
#ifndef NDEBUG
	// fprintf(stderr, "veryifying zend engine has opline == %u...\n", i);
	if (!phpllvm_check_opline(execute_data, i TSRMLS_CC))
		fprintf(stderr, "Zend engine has opline == %u, while we think it's %u\n", execute_data->opline - execute_data->op_array->opcodes, i);
#endif
}

void phpllvm_set_executor(zend_execute_t new_execute) {
	zend_execute = new_execute;
}

opcode_handler_t phpllvm_get_opcode_handler(zend_op* op) {
	zend_op op_copy = *op;
	zend_init_opcodes_handlers();
	zend_vm_set_opcode_handler(&op_copy);
	return op_copy.handler;
}