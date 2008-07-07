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
#define EX(element) stack_data->execute_data->element

struct _execute_stack_data {
	zend_op_array *op_array;
	zend_execute_data *execute_data;
	zend_bool nested;
	zend_bool original_in_execution;
};

int phpllvm_executor_exception_exists(TSRMLS_D) {
#ifndef NDEBUG
	if (EG(exception))
		fprintf(stderr, "Zend engine exception exists.\n");
#endif
	return EG(exception) != 0;
}

execute_stack_data *phpllvm_init_executor(zend_op_array *op_array TSRMLS_DC) {
	execute_stack_data *stack_data = emalloc(sizeof(execute_stack_data));
	stack_data->execute_data = NULL;
	stack_data->op_array = op_array;
	stack_data->nested = 0;
	stack_data->original_in_execution = EG(in_execution);

	EG(in_execution) = 1;

	return stack_data;
}

void phpllvm_create_execute_data(execute_stack_data *stack_data TSRMLS_DC) {
	/* has to be allocated on the Zend VM stack because the ZEND_RETURN handler
 		frees arguments to the function by decreasing the Zend VM stack pointer. */
	stack_data->execute_data = (zend_execute_data *)zend_vm_stack_alloc(
		sizeof(zend_execute_data) +
		sizeof(zval**) * stack_data->op_array->last_var * (EG(active_symbol_table) ? 1 : 2) +
		sizeof(temp_variable) * stack_data->op_array->T TSRMLS_CC);

	EX(CVs) = (zval***)((char*)stack_data->execute_data + sizeof(zend_execute_data));
	memset(EX(CVs), 0, sizeof(zval**) * stack_data->op_array->last_var);
	EX(Ts) = (temp_variable *)(EX(CVs) + stack_data->op_array->last_var * (EG(active_symbol_table) ? 1 : 2));
	EX(fbc) = NULL;
	EX(called_scope) = NULL;
	EX(object) = NULL;
	EX(old_error_reporting) = NULL;
	EX(op_array) = stack_data->op_array;
	EX(symbol_table) = EG(active_symbol_table);
	EX(prev_execute_data) = EG(current_execute_data);
	EG(current_execute_data) = stack_data->execute_data;
	EX(nested) = stack_data->nested;
	stack_data->nested = 1;

	if (stack_data->op_array->start_op) {
		stack_data->execute_data->opline = stack_data->op_array->start_op;
	} else {
		stack_data->execute_data->opline = stack_data->op_array->opcodes;
	}

	if (stack_data->op_array->this_var != -1 && EG(This)) {
 		Z_ADDREF_P(EG(This)); /* For $this pointer */
		if (!EG(active_symbol_table)) {
			EX(CVs)[stack_data->op_array->this_var] = (zval**)EX(CVs) + (stack_data->op_array->last_var + stack_data->op_array->this_var);
			*EX(CVs)[stack_data->op_array->this_var] = EG(This);
		} else {
			if (zend_hash_add(EG(active_symbol_table), "this", sizeof("this"), &EG(This), sizeof(zval *), (void**)&EX(CVs)[stack_data->op_array->this_var])==FAILURE) {
				Z_DELREF_P(EG(This));
			}
		}
	}

	EG(opline_ptr) = &EX(opline);

	EX(function_state).function = (zend_function *) stack_data->op_array;
	EX(function_state).arguments = NULL;
}

zend_execute_data *phpllvm_get_execute_data(execute_stack_data *stack_data) {
	return stack_data->execute_data;
}

void phpllvm_pre_vm_return(execute_stack_data *stack_data TSRMLS_DC) {
	EG(in_execution) = stack_data->original_in_execution;
	efree(stack_data);
}

void phpllvm_pre_vm_enter(execute_stack_data *stack_data TSRMLS_DC) {
	stack_data->op_array = EG(active_op_array);
}

void phpllvm_pre_vm_leave(execute_stack_data *stack_data TSRMLS_DC) {
	stack_data->execute_data = EG(current_execute_data);
}

int phpllvm_get_opline_number(execute_stack_data *stack_data) {
	return stack_data->execute_data->opline - stack_data->execute_data->op_array->opcodes;
}

void phpllvm_verify_opline(execute_stack_data *stack_data, int i) {
#ifndef NDEBUG
	// fprintf(stderr, "veryifying zend engine has opline == %u...\n", i);
	if (stack_data->execute_data->opline != stack_data->execute_data->op_array->opcodes + i)
		fprintf(stderr, "Zend engine has opline == %u, while we think it's %u\n", stack_data->execute_data->opline - stack_data->execute_data->op_array->opcodes, i);
#endif
}

opcode_handler_t phpllvm_get_opcode_handler(zend_op* op) {
	zend_op op_copy = *op;
	zend_init_opcodes_handlers();
	zend_vm_set_opcode_handler(&op_copy);
	return op_copy.handler;
}

void phpllvm_fix_jumps(zend_op_array *op_array, zend_op* orig_first) {
	/* TODO: It would be nicer to initialize these addresses correctly directly but it's tricky to get the real memory address of the new array while still generating the initializer. */

	int i;
	for (i=0; i < op_array->last; ++i) {
		zend_op *zo = op_array->opcodes + i;

		/*Note: We need to update the "absolute" jump addresses that are not given as offsets from the first op. */
		switch (zo->opcode) {
			case ZEND_JMP:
				zo->op1.u.jmp_addr = op_array->opcodes + (zo->op1.u.jmp_addr - orig_first);
				break;
			case ZEND_JMPZ:
			case ZEND_JMPNZ:
			case ZEND_JMPZ_EX:
			case ZEND_JMPNZ_EX:
				zo->op2.u.jmp_addr = op_array->opcodes + (zo->op2.u.jmp_addr - orig_first);
				break;
			default:
				break;
		}
	}
}
