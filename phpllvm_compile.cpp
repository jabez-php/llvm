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

#include "phpllvm_compile.h"
#include "phpllvm_execute.h"
#include "phpllvm_handler_lookup.h"

#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/CallingConv.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/ExecutionEngine/GenericValue.h>

using namespace llvm;
using namespace phpllvm;

// from: static inline zend_brk_cont_element* zend_brk_cont(zval *nest_levels_zval, int array_offset, zend_op_array *op_array, temp_variable *Ts TSRMLS_DC)   [zend_execute.cpp]
static zend_brk_cont_element* get_brk_cont_target(zval *nest_levels_zval, int array_offset, zend_op_array *op_array, temp_variable *Ts TSRMLS_DC) {
	zval tmp;
	int nest_levels, original_nest_levels;
	zend_brk_cont_element *jmp_to;

	if (nest_levels_zval->type != IS_LONG) {
		tmp = *nest_levels_zval;
		zval_copy_ctor(&tmp);
		convert_to_long(&tmp);
		nest_levels = tmp.value.lval;
	} else {
		nest_levels = nest_levels_zval->value.lval;
	}
	original_nest_levels = nest_levels;
	do {
		if (array_offset==-1) {
			zend_error_noreturn(E_ERROR, "Cannot break/continue %d level%s", original_nest_levels, (original_nest_levels == 1) ? "" : "s");
		}
		jmp_to = &op_array->brk_cont_array[array_offset];
		/*
		skip freeing stuff since this is compile time
		*/
		array_offset = jmp_to->parent;
	} while (--nest_levels > 0);
	return jmp_to;
}

/* Taken from Zend/zend_vm_execute.h */
#define _CONST_CODE  0
#define _TMP_CODE    1
#define _VAR_CODE    2
#define _UNUSED_CODE 3
#define _CV_CODE     4
static int opcode_handler_decode(zend_op *opline)
{
	static const int apc_vm_decode[] = {
		_UNUSED_CODE, /* 0              */
		_CONST_CODE,  /* 1 = IS_CONST   */
		_TMP_CODE,    /* 2 = IS_TMP_VAR */
		_UNUSED_CODE, /* 3              */
		_VAR_CODE,    /* 4 = IS_VAR     */
		_UNUSED_CODE, /* 5              */
		_UNUSED_CODE, /* 6              */
		_UNUSED_CODE, /* 7              */
		_UNUSED_CODE, /* 8 = IS_UNUSED  */
		_UNUSED_CODE, /* 9              */
		_UNUSED_CODE, /* 10             */
		_UNUSED_CODE, /* 11             */
		_UNUSED_CODE, /* 12             */
		_UNUSED_CODE, /* 13             */
		_UNUSED_CODE, /* 14             */
		_UNUSED_CODE, /* 15             */
		_CV_CODE      /* 16 = IS_CV     */
	};
	return (opline->opcode * 25) + (apc_vm_decode[opline->op1.op_type] * 5) + apc_vm_decode[opline->op2.op_type];
}

Function* phpllvm::compile_op_array(zend_op_array *op_array, char* fn_name, Module* mod, ExecutionEngine* engine TSRMLS_DC) {

	// fprintf(stderr, "compiling %s\n", fn_name);

	/* Fetch the precompiled functions defined in the template */
	Function* executor_exception_exists = mod->getFunction("phpllvm_executor_exception_exists");
	Function* init_executor = mod->getFunction("phpllvm_init_executor");
	Function* create_execute_data = mod->getFunction("phpllvm_create_execute_data");
	Function* verify_opline = mod->getFunction("phpllvm_verify_opline");
	Function* check_opline = mod->getFunction("phpllvm_check_opline");
	Function* get_handler = mod->getFunction("phpllvm_get_opcode_handler");

	/* Define the main function for the op_array (fn_name)
		Takes op_array* and TSRMLS_D as arguments. */
	std::vector<const Type*> arg_types;
	arg_types.push_back(create_execute_data->getFunctionType()->getParamType(0));
	arg_types.push_back(create_execute_data->getFunctionType()->getParamType(1));
	FunctionType *process_oparray_type = FunctionType::get(Type::VoidTy, arg_types, false);
	Function *process_oparray = Function::Create(process_oparray_type,
												Function::ExternalLinkage,
												fn_name,
												mod);

	/* The opcode handlers take zend_execute_data* and TSRMLS_D as arguments.
	 	(Same as phpllvm_init_executor.) */
	arg_types.clear();
	arg_types.push_back(init_executor->getFunctionType()->getParamType(0));
	arg_types.push_back(init_executor->getFunctionType()->getParamType(1));
	FunctionType *handler_type = FunctionType::get(Type::Int32Ty, arg_types, false);

	Function::arg_iterator args_i = process_oparray->arg_begin();
	Value* op_array_ref = args_i++;
	Value* tsrlm_ref = args_i;

	
	/* Create the entry, (pre_)op_code, and return blocks */
	BasicBlock* entry = BasicBlock::Create("entry", process_oparray);
	BasicBlock* init = BasicBlock::Create("init", process_oparray);
	BasicBlock* ret = BasicBlock::Create("ret", process_oparray);

	BasicBlock **pre_op_blocks, **op_blocks;
	pre_op_blocks = (BasicBlock**) emalloc(
		sizeof(BasicBlock*) * (op_array->last+1));
	op_blocks = (BasicBlock**) emalloc(
		sizeof(BasicBlock*) * (op_array->last+1));
	for (int i = 0; i < op_array->last; i++) {
		pre_op_blocks[i] = BasicBlock::Create("pre_op_block", process_oparray);
		op_blocks[i] = BasicBlock::Create("op_block", process_oparray);
	}
	// avoid special cases in branching for the last opblock
	pre_op_blocks[op_array->last] = ret;
	op_blocks[op_array->last] = ret;

	/* Populate the entry block */
	IRBuilder builder(entry);

	// return if EG(exception) is set
	Value* exception_exists = builder.CreateCall(
				executor_exception_exists,
				tsrlm_ref,
				"exception_exists");
	Value* no_exception = builder.CreateICmpEQ(exception_exists,
												ConstantInt::get(Type::Int32Ty, 0),
												"no_exception");

	builder.CreateCondBr(no_exception, init, ret);

	// init execute data and the executor
	builder.SetInsertPoint(init);

	Value* execute_data = builder.CreateCall2(create_execute_data,
				op_array_ref,
				tsrlm_ref,
				"execute_data");

	builder.CreateCall2(init_executor,
				execute_data,
				tsrlm_ref);

	int start = (op_array->start_op)? op_array->start_op - op_array->opcodes : 0;
	builder.CreateBr(pre_op_blocks[start]);

	/* Populate the final return block */
	builder.SetInsertPoint(ret);

	builder.CreateRetVoid();

	/* populate each pre_op_code block */
	for (int i = 0; i < op_array->last; i++) {
		builder.SetInsertPoint(pre_op_blocks[i]);

#ifndef NDEBUG
		if (op_array->opcodes[i].opcode != ZEND_OP_DATA) {
			// verify that execute_data->opline is set to i'th op_code
			builder.CreateCall3(verify_opline,
							execute_data,
							ConstantInt::get(Type::Int32Ty, i),
							tsrlm_ref);
		}
#endif

		// and jump to the corresponding block
		builder.CreateBr(op_blocks[i]);
	}

	/* populate each op_code block */
	for (int i = 0; i < op_array->last; i++) {
		zend_op* op = op_array->opcodes + i;

		builder.SetInsertPoint(op_blocks[i]);

		if (op->opcode == ZEND_OP_DATA) {
			// ZEND_OP_DATA only provides data for the previous opcode.
			builder.CreateBr(pre_op_blocks[i + 1]);
			continue;
		}

		/* Need to force the compilation of the handler because otherwise phpllvm_get_handler only
			returns the address of the call to the JIT compilation hook, which getGlobalValueAtAddress
			doesn't recognize as the Function. */
		// TODO: Do this without phpllvm_get_function_name(), preferably from within phpllvm_get_handler
		engine->clearAllGlobalMappings();
		const char* handler_name = phpllvm_get_function_name(opcode_handler_decode(op));
		engine->getPointerToGlobal(mod->getFunction(handler_name));
		/* phpllvm_get_handler will return the old address unless recompiled. */
		engine->recompileAndRelinkFunction(get_handler);

		/* Use the reverse lookup from an actual memory address (handler_raw)
			to the corresponding llvm::Function */
		std::vector<GenericValue> args;
		args.push_back(PTOGV(op));
		void* handler_raw = GVTOP(engine->runFunction(get_handler, args));
		Function* handler = (Function*) engine->getGlobalValueAtAddress(handler_raw);

		/* Always execute the op_code using the Zend handler,
 			even for JMPs, to keep the engine in sync. */
		Value *handler_args[] = { execute_data, tsrlm_ref };
	    CallInst* result = CallInst::Create(handler, handler_args, handler_args+2, "execute_result", op_blocks[i]);
		result->setCallingConv(handler->getCallingConv()); // PHP 5.3 uses fastcc


		// determine where to jump
		if (op->opcode == ZEND_JMP) {

			// jump to the corresponding pre_op_block
			builder.CreateBr(pre_op_blocks[op->op1.u.jmp_addr - op_array->opcodes]);

		} else if (op->opcode == ZEND_JMPZ
				|| op->opcode == ZEND_JMPNZ
				|| op->opcode == ZEND_JMPZNZ
				|| op->opcode == ZEND_JMPZ_EX
				|| op->opcode == ZEND_JMPNZ_EX
				|| op->opcode == ZEND_JMP_SET
				|| op->opcode == ZEND_FE_FETCH
				|| op->opcode == ZEND_FE_RESET) {

			int target1, target2;

			if (op->opcode == ZEND_JMPZNZ) {
				target1 = op->op2.u.opline_num;
				target2 = op->extended_value;
			} else if (op->opcode == ZEND_FE_FETCH) {
				target1 = op->op2.u.opline_num;
				target2 = i + 2;
			} else if (op->opcode == ZEND_FE_RESET) {
				target1 = op->op2.u.opline_num;
				target2 = i + 1;
			} else {
				target1 = op->op2.u.jmp_addr - op_array->opcodes;
				target2 = i+1;
			}

			// check which line the engine wants to continue on (target1 or target2)
			result = builder.CreateCall3(check_opline,
								execute_data,
								ConstantInt::get(Type::Int32Ty, target1),
								tsrlm_ref,
								"target1");

			SwitchInst* switch_ref = builder.CreateSwitch(result, pre_op_blocks[target2], 1);
			switch_ref->addCase(ConstantInt::get(Type::Int32Ty, 1), pre_op_blocks[target1]);

		} else if (op->opcode == ZEND_BRK) {

			zend_brk_cont_element *el = get_brk_cont_target(&op->op2.u.constant, op->op1.u.opline_num,
			                   op_array, NULL TSRMLS_CC);

			// jump to the end of the loop pre_op_block
			builder.CreateBr(pre_op_blocks[el->brk]);

		} else if (op->opcode == ZEND_CONT) {

			zend_brk_cont_element *el = get_brk_cont_target(&op->op2.u.constant, op->op1.u.opline_num,
			                   op_array, NULL TSRMLS_CC);

			// jump to the beginning of the loop pre_op_block
			builder.CreateBr(pre_op_blocks[el->cont]);

		} else {

			// proceed to next op_code unless the handler returned a non-zero result
			Value* success = builder.CreateICmpEQ(result, ConstantInt::get(Type::Int32Ty, 0), "success");
			builder.CreateCondBr(success, pre_op_blocks[i+1], ret);

		}

	}

	efree(pre_op_blocks);
	efree(op_blocks);

	/* engine->clearAllGlobalMappings() clears zend_execute* as well... */
	std::vector<GenericValue> args;
	args.push_back(PTOGV((void*) phpllvm::execute));
	engine->runFunction(mod->getFunction("phpllvm_set_executor"), args);

	return process_oparray;
}
