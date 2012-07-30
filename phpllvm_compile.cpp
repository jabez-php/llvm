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

#include "phpllvm_compile.h"
#include "phpllvm_execute.h"

#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/CallingConv.h>
#include <llvm/Support/TypeBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/Analysis/Verifier.h>


#include <vector>

// TODO: define this in config.m4 for llvm>=3.1
#ifdef HAVE_IRBUILDER_IN_ROOT
#include <llvm/IRBuilder.h>
#else
#include <llvm/Support/IRBuilder.h>
#endif

#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/LLVMContext.h>

using namespace llvm;
using namespace phpllvm;

// currently type names are different when compiled with clang or llvm-gcc
#ifdef COMPILED_WITH_CLANG
# define STRUCT(s) "struct._" s
# define UNION(s) "union._" s
#else
# define STRUCT(s) "struct." s
# define UNION(s) "struct." s
#endif

// Abstraction for the change in znode_op in PHP 5.4
#if PHP_API_VERSION >= 20100412
#define ZNODE_OPLINE_NUM(zn) ((zn).opline_num)
#define ZNODE_JMP_ADDR(zn) ((zn).jmp_addr)
#define ZNODE_CONSTANT_ADDR(zn) ((zn).zv)
#else
#define ZNODE_OPLINE_NUM(zn) ((zn).u.opline_num)
#define ZNODE_JMP_ADDR(zn) ((zn).u.jmp_addr)
#define ZNODE_CONSTANT_ADDR(zn) (&(zn).constant)
#endif

extern std::map<void*, Function*> op_handlers;

static CallInst * make_handler_call(ExecutionEngine * engine, zend_op * op, 
		BasicBlock * insert_at_end, Value * execute_data, Function * get_handler, Value * tsrlm_ref);

static Function * get_function(Module * mod, const char * name TSRMLS_DC);

//static GlobalVariable* dump_op_array(zend_op_array* val, Module* mod, ExecutionEngine* engine);

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

Function* phpllvm::compile_op_array(zend_op_array *op_array, char* fn_name, Module* mod, ExecutionEngine* engine TSRMLS_DC) {

	// fprintf(stderr, "compiling %s\n", fn_name);

	/* Fetch the precompiled functions defined in the template */
	Function* get_exception_pp = get_function(mod, "phpllvm_get_exception_pp");
	Function* init_executor = get_function(mod, "phpllvm_init_executor");
	Function* create_execute_data = get_function(mod, "phpllvm_create_execute_data");
	Function* pre_vm_return = get_function(mod, "phpllvm_pre_vm_return");
	Function* get_execute_data = get_function(mod, "phpllvm_get_execute_data");
	Function* get_opline_number = get_function(mod, "phpllvm_get_opline_number");
	Function* get_handler = get_function(mod, "phpllvm_get_opcode_handler");
	Function* handle_invalid_reposition = get_function(mod, "phpllvm_handle_invalid_reposition");

	LLVMContext & context = getGlobalContext();
	IntegerType* int32ty = Type::getInt32Ty(context);

	Type* op_array_type = PointerType::getUnqual(mod->getTypeByName(STRUCT("zend_op_array")));

	/* Dump the op_array into an LLVM Constant */
	// TODO:
// 	/* Value* op_array_ref = */ dump_op_array(op_array, mod, engine);

	/* Define the main function for the op_array (fn_name)
		Takes op_array* and TSRMLS_D as arguments. */
	std::vector<Type*> arg_types;
	arg_types.push_back(int32ty); // dummy
	arg_types.push_back(op_array_type);
#ifdef ZTS
	const Type* tsrmls_type = init_executor->getFunctionType()->getParamType(1);
	arg_types.push_back(tsrmls_type);
#endif

	FunctionType *process_oparray_type = FunctionType::get(
		Type::getVoidTy(context), ArrayRef<Type*>(arg_types), false);
	Function *process_oparray = Function::Create(process_oparray_type,
												Function::ExternalLinkage,
												fn_name,
												mod);

	Function::arg_iterator args_i = process_oparray->arg_begin();
	args_i++; // dummy
	Value* op_array_ref = args_i;
	Value* tsrlm_ref = NULL;
#ifdef ZTS
	tsrlm_ref = ++args_i;
#endif

	/* Create the init, (pre_)op_code, and return blocks */
	BasicBlock* entry = BasicBlock::Create(context, "entry", process_oparray);
	BasicBlock* init = BasicBlock::Create(context, "init", process_oparray);
	BasicBlock* vm_enter = BasicBlock::Create(context, "vm_enter", process_oparray);
	BasicBlock* pre_vm_return_block = BasicBlock::Create(context, "pre_vm_return", process_oparray);
	BasicBlock* invalid_reposition_block = BasicBlock::Create(context, "invalid_reposition", process_oparray);
	BasicBlock* ret = BasicBlock::Create(context, "ret", process_oparray);

	std::vector<BasicBlock*>op_blocks(op_array->last+1);
	std::vector<BasicBlock*>op_blocks_b(op_array->last);

	for (zend_uint i = 0; i < op_array->last; ++i) {
		op_blocks[i] = BasicBlock::Create(context, "op_block_a", process_oparray);
		op_blocks_b[i] = BasicBlock::Create(context, "op_block_b", process_oparray);
	}
	// avoid special cases in branching for the last opblock
	op_blocks[op_array->last] = ret;

	BasicBlock* handle_exception_block = BasicBlock::Create(context, "handle_exception", process_oparray);
	BasicBlock* exception_continue  = BasicBlock::Create(context, "exception_continue", process_oparray);

	// preserve names in the output only when in debug mode. otherwise speedup the process
#ifdef DEBUG_PHPLLVM
	IRBuilder<true> builder(entry);
#else
	IRBuilder<false> builder(entry);
#endif

#ifdef ZTS
# define CREATE_TSRMLS_CALL0(f) builder.CreateCall(f, tsrlm_ref)
# define CREATE_TSRMLS_CALL(f, arg) builder.CreateCall2(f, arg, tsrlm_ref)
# define CREATE_TSRMLS_CALL2(f, arg1, arg2) builder.CreateCall3(f, arg1, arg2, tsrlm_ref)
#else
# define CREATE_TSRMLS_CALL0(f) builder.CreateCall(f)
# define CREATE_TSRMLS_CALL(f, arg) builder.CreateCall(f, arg)
# define CREATE_TSRMLS_CALL2(f, arg1, arg2) builder.CreateCall2(f, arg1, arg2)
#endif

	// Populate the init block

	/* Return straight away if EG(exception) is set */
	Value * exception_pp = CREATE_TSRMLS_CALL0(get_exception_pp);
	LoadInst * exception = builder.CreateLoad(exception_pp);
	ConstantPointerNull * zval_null = ConstantPointerNull::get(
			cast<PointerType>(exception->getType()));
	Value * no_exception = builder.CreateICmpEQ(exception, zval_null, "no_exception");
	builder.CreateCondBr(no_exception, init, ret);

	/* Populate the init block */
	Type * execute_stack_data_ty = mod->getTypeByName(STRUCT("execute_stack_data"));
	if (!execute_stack_data_ty) {
		zend_error_noreturn(E_ERROR, "phpllvm: unable to get type for struct _execute_stack_data");
	}
	builder.SetInsertPoint(init);
	Value *stack_data = builder.CreateAlloca(execute_stack_data_ty, NULL, "stack_data");
	CREATE_TSRMLS_CALL2(init_executor, stack_data, op_array_ref);

	builder.CreateBr(vm_enter);

	/* Populate the vm_enter block */
	builder.SetInsertPoint(vm_enter);
	CREATE_TSRMLS_CALL(create_execute_data, stack_data);

	// Cache the execute_data pointer in a local value
	Value *execute_data = builder.CreateCall(get_execute_data, stack_data, "execute_data");

	builder.CreateBr(op_blocks[0]);

	/* Populate the pre_vm_return block */
	builder.SetInsertPoint(pre_vm_return_block);
	CREATE_TSRMLS_CALL(pre_vm_return, stack_data);
	builder.CreateRetVoid();

	/* Populate the invalid reposition block */
	builder.SetInsertPoint(invalid_reposition_block);
	CREATE_TSRMLS_CALL(handle_invalid_reposition, stack_data);
	builder.CreateBr(ret);

	/* Populate the ret block */
	builder.SetInsertPoint(ret);
	builder.CreateRetVoid();


	/* populate each op_code block */
	for (zend_uint i = 0; i < op_array->last; ++i) {
		zend_op* op = op_array->opcodes + i;

		builder.SetInsertPoint(op_blocks[i]);

		if (op->opcode == ZEND_OP_DATA) {
			// ZEND_OP_DATA only provides data for the previous opcode.
			builder.CreateBr(op_blocks[i + 1]);
			builder.SetInsertPoint(op_blocks_b[i]);
			builder.CreateUnreachable();
			continue;
		}

#ifdef DEBUG_PHPLLVM
		// verify that execute_data->opline is set to i'th op_code
		Function* verify_opline = get_function(mod, "phpllvm_verify_opline");
		builder.CreateCall2(verify_opline, stack_data, ConstantInt::get(int32ty, i));
#endif

		CallInst * result = make_handler_call(engine, op, op_blocks[i], execute_data, 
				get_handler, tsrlm_ref);

		// Check for exceptions
		exception = builder.CreateLoad(exception_pp);
		no_exception = builder.CreateICmpEQ(exception, zval_null);
		builder.CreateCondBr(no_exception, op_blocks_b[i], handle_exception_block);
		builder.SetInsertPoint(op_blocks_b[i]);

		// determine where to jump
		if (op->opcode == ZEND_JMP) {

			// jump to the corresponding op_block
			builder.CreateBr(op_blocks[ZNODE_JMP_ADDR(op->op1) - op_array->opcodes]);

		} else if (op->opcode == ZEND_JMPZ
				|| op->opcode == ZEND_JMPNZ
				|| op->opcode == ZEND_JMPZNZ
				|| op->opcode == ZEND_JMPZ_EX
				|| op->opcode == ZEND_JMPNZ_EX
				|| op->opcode == ZEND_JMP_SET
				|| op->opcode == ZEND_JMP_SET_VAR
				|| op->opcode == ZEND_FE_FETCH
				|| op->opcode == ZEND_FE_RESET
				|| op->opcode == ZEND_NEW) {

			int target1, target2;

			if (op->opcode == ZEND_JMPZNZ) {
				target1 = ZNODE_OPLINE_NUM(op->op2);
				target2 = op->extended_value;
			} else if (op->opcode == ZEND_FE_FETCH) {
				target1 = ZNODE_OPLINE_NUM(op->op2);
				target2 = i + 2;
			} else if (op->opcode == ZEND_FE_RESET || op->opcode == ZEND_NEW) {
				target1 = ZNODE_OPLINE_NUM(op->op2);
				target2 = i + 1;
			} else {
				target1 = ZNODE_JMP_ADDR(op->op2) - op_array->opcodes;
				target2 = i+1;
			}

			// check which line the engine wants to continue on (target1 or target2)
			// TODO: add assert in debug mode to check if get_opline_number() returned target1 or target2
			Value* op_number = builder.CreateCall(get_opline_number, stack_data, "target");
			Value *cond = builder.CreateICmpEQ(op_number, ConstantInt::get(int32ty, target1));
			builder.CreateCondBr(cond, op_blocks[target1], op_blocks[target2]);

		} else if (op->opcode == ZEND_BRK 
				|| op->opcode == ZEND_GOTO) {

			zend_brk_cont_element *el = get_brk_cont_target(ZNODE_CONSTANT_ADDR(op->op2), 
				ZNODE_OPLINE_NUM(op->op1), op_array, NULL TSRMLS_CC);

			// jump to the end of the loop op_block
			builder.CreateBr(op_blocks[el->brk]);

		} else if (op->opcode == ZEND_CONT) {

			zend_brk_cont_element *el = get_brk_cont_target(ZNODE_CONSTANT_ADDR(op->op2), 
				ZNODE_OPLINE_NUM(op->op1), op_array, NULL TSRMLS_CC);

			// jump to the beginning of the loop op_block
			builder.CreateBr(op_blocks[el->cont]);

		} else {
			// proceed to next op_code unless the handler returned a non-zero result
			SwitchInst* switch_ref = builder.CreateSwitch(result, invalid_reposition_block, 3);
			switch_ref->addCase(ConstantInt::get(int32ty, 0), op_blocks[i+1]);
			switch_ref->addCase(ConstantInt::get(int32ty, 1), pre_vm_return_block);
		}
	}

	// Handle exceptions
	builder.SetInsertPoint(handle_exception_block);

	// First call the exception op handler
	zend_op exception_op = EG(exception_op)[0];
	CallInst * result = make_handler_call(engine, &exception_op, handle_exception_block, 
			execute_data, get_handler, tsrlm_ref);

	// Take the normal action for return, enter or leave
	SwitchInst* switch_ref = builder.CreateSwitch(result, invalid_reposition_block, 3);
	switch_ref->addCase(ConstantInt::get(int32ty, 0), exception_continue);
	switch_ref->addCase(ConstantInt::get(int32ty, 1), pre_vm_return_block);
	
	// On continue, we need to jump to the catch block indicated by execute_data->opline
	builder.SetInsertPoint(exception_continue);
	Value* catch_op_ref = builder.CreateCall(get_opline_number, stack_data, "current");
	switch_ref = builder.CreateSwitch(catch_op_ref, invalid_reposition_block, op_array->last_try_catch);
	for ( int i = 0; i < op_array->last_try_catch; i++ ) {
		int catch_op = op_array->try_catch_array[i].catch_op;
		switch_ref->addCase(ConstantInt::get(int32ty, catch_op), op_blocks[catch_op]);
	}

	return process_oparray;
}

static Function * get_function(Module * mod, const char * name TSRMLS_DC) {
	Function * f = mod->getFunction(name);
	if (!f) {
		zend_error_noreturn(E_ERROR, "phpllvm: unable to get template function %s", name);
	}
	return f;
}

static void add_padding(std::vector<Constant*> &v, unsigned bytes)
{
	LLVMContext & context = getGlobalContext();	
	for (unsigned i = 0; i < bytes; ++i) {
		v.push_back(ConstantInt::get(Type::getInt8Ty(context), 0));
	}
}

static CallInst * make_handler_call(ExecutionEngine * engine, zend_op * op, 
		BasicBlock * insert_at_end, Value * execute_data, Function * get_handler, Value * tsrlm_ref) 
{
	/* Use the reverse lookup from an actual memory address (handler_raw)
		to the corresponding llvm::Function */
	std::vector<GenericValue> args;
	args.push_back(PTOGV(op));
	void* handler_raw = GVTOP(engine->runFunction(get_handler, args));
	std::map<void*, Function*>::iterator iter = op_handlers.find(handler_raw);
	if (iter == op_handlers.end()) {
		zend_error_noreturn(E_ERROR, "phpllvm: unable to get global value for handler %d at address %p",
			op->opcode, handler_raw);
	}
	Function *handler = iter->second;

	/* Always execute the op_code using the Zend handler,
		even for JMPs, to keep the engine in sync. */
	Value *handler_args[] = {
		execute_data,
#ifdef ZTS
		tsrlm_ref
#endif
	};
	CallInst* result = CallInst::Create(
			handler, ArrayRef<Value*>(handler_args), "execute_result", insert_at_end);
	result->setCallingConv(handler->getCallingConv()); // PHP 5.3 uses fastcc
	return result;
}

#if 0
static GlobalVariable* dump_class_entry(zend_class_entry* class_entry, Module* mod) {
	Type* class_entry_type = mod->getTypeByName(STRUCT("zend_class_entry"));

	Constant* initializer = Constant::getNullValue(class_entry_type);

	return new GlobalVariable(initializer->getType(), true, GlobalValue::InternalLinkage, initializer, "class_entry", mod);
}

static GlobalVariable* dump_function(zend_function* fn, Module* mod) {
	Type* function_type = mod->getTypeByName(STRUCT("zend_function"));

	Constant* initializer = Constant::getNullValue(function_type);

	return new GlobalVariable(initializer->getType(), true, GlobalValue::InternalLinkage, initializer, "zend_fn", mod);
}

static GlobalVariable* dump_arg_info_array(zend_arg_info* info_array, int count, Module* mod) {
	const StructType* arg_info_type = cast<const StructType>(mod->getTypeByName(STRUCT("zend_arg_info")));
	LLVMContext & context = getGlobalContext();	
	IntegerType* int8ty = Type::getInt8Ty(context);
	IntegerType* int32ty = Type::getInt32Ty(context);

	std::vector<Constant*> infos;
	for(int i = 0; i < count; i++) {
		// typedef struct _zend_arg_info {
		// 	const char *name;
		// 	zend_uint name_len;
		// 	const char *class_name;
		// 	zend_uint class_name_len;
		// 	zend_bool array_type_hint;
		// 	zend_bool allow_null;
		// 	zend_bool pass_by_reference;
		// 	zend_bool return_reference;
		// 	int required_num_args;
		// } zend_arg_info;
		// %struct.zend_arg_info = type { i8*, i32, i8*, i32, i8, i8, i8, i8, i32 }

		std::vector<Constant*> zero_indices(2, ConstantInt::get(int32ty, 0)); // used by getGetElementPointer()

		std::vector<Constant*> info_members;

		if (info_array[i].name) {
			Constant* string = ConstantArray::get(info_array[i].name);
			GlobalVariable* var = new GlobalVariable(string->getType(), true, GlobalValue::InternalLinkage, string, "arg_info_name", mod);
			info_members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
		} else
			info_members.push_back(ConstantPointerNull::get(PointerType::getUnqual(int8ty)));

		info_members.push_back(ConstantInt::get(int32ty, info_array[i].name_len));

		if (info_array[i].class_name) {
			Constant* string = ConstantArray::get(info_array[i].class_name);
			GlobalVariable* var = new GlobalVariable(string->getType(), true, GlobalValue::InternalLinkage, string, "arg_info_class_name", mod);
			info_members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
		} else
			info_members.push_back(ConstantPointerNull::get(PointerType::getUnqual(int8ty)));

		info_members.push_back(ConstantInt::get(int32ty, info_array[i].class_name_len));
		info_members.push_back(ConstantInt::get(int8ty, info_array[i].array_type_hint));
		info_members.push_back(ConstantInt::get(int8ty, info_array[i].allow_null));
		info_members.push_back(ConstantInt::get(int8ty, info_array[i].pass_by_reference));
		info_members.push_back(ConstantInt::get(int8ty, info_array[i].return_reference));
		info_members.push_back(ConstantInt::get(int32ty, info_array[i].required_num_args));

		infos.push_back(ConstantStruct::get(arg_info_type, info_members));
	}

	const ArrayType *info_array_type = ArrayType::get(arg_info_type, count);
	Constant* initializer = ConstantArray::get(info_array_type, ArrayRef<Constant*>(infos));

	return new GlobalVariable(initializer->getType(), true, GlobalValue::InternalLinkage, initializer, "arg_infos", mod);
}

#ifndef COMPILED_WITH_CLANG
static Constant* copy_zval(zval* zval, Module* mod) {
	// struct _zval_struct {
	// 	/* Variable information */
	// 	zvalue_value value;		/* value */
	// 	zend_uint refcount__gc;
	// 	zend_uchar type;	/* active type */
	// 	zend_uchar is_ref__gc;
	// };
	// %struct.zval = type { %struct.zvalue_value, i32, i8, i8 }

	// typedef union _zvalue_value {
	// 	long lval;					/* long value */
	// 	double dval;				/* double value */
	// 	struct {
	// 		char *val;
	// 		int len;
	// 	} str;
	// 	HashTable *ht;				/* hash table value */
	// 	zend_object_value obj;
	// } zvalue_value;
	// %struct.zvalue_value = type { double }

#ifdef COMPILED_WITH_CLANG
	const StructType* zval_type = cast<const StructType>(mod->getTypeByName(STRUCT("zval_struct")));
#else
	const StructType* zval_type = cast<const StructType>(mod->getTypeByName(STRUCT("zval")));
#endif
	const StructType* zvalue_value_type = cast<const StructType>(mod->getTypeByName(UNION("zvalue_value")));
	LLVMContext & context = getGlobalContext();	
	const IntegerType* int8ty = Type::getInt8Ty(context);
	const IntegerType* int32ty = Type::getInt32Ty(context);
	const Type* doubleTy = Type::getDoubleTy(context);

	std::vector<Constant*> zvalue_value_members;
	zvalue_value_members.push_back(ConstantFP::get(doubleTy, zval->value.dval)); // TODO: Could this change some bits if the value is not actually a double?

	std::vector<Constant*> zval_members;
	zval_members.push_back(ConstantStruct::get(zvalue_value_type, zvalue_value_members));
	zval_members.push_back(ConstantInt::get(int32ty, zval->refcount__gc));
	zval_members.push_back(ConstantInt::get(int8ty, zval->type));
	zval_members.push_back(ConstantInt::get(int8ty, zval->is_ref__gc));

	return ConstantStruct::get(zval_type, zval_members);
}
#endif

static Constant* copy_znode(znode* node, Module* mod) {
	// typedef struct _znode {
	// 	int op_type;
	// 	union {
	// 		zval constant;
	// 
	// 		zend_uint var;
	// 		zend_uint opline_num; /*  Needs to be signed */
	// 		zend_op_array *op_array;
	// 		zend_op *jmp_addr;
	// 		struct {
	// 			zend_uint var;	/* dummy */
	// 			zend_uint type;
	// 		} EA;
	// 	} u;
	// } znode;

	// llvm-gcc: %struct.znode = type { i32, %struct.zend_declarables }
	// clang:    %struct._znode = type <{ i32, %union.anon }>

	const StructType* znode_type = cast<const StructType>(mod->getTypeByName(STRUCT("znode")));
	LLVMContext & context = getGlobalContext();	
	const IntegerType* int8ty = Type::getInt8Ty(context);
	const IntegerType* int32ty = Type::getInt32Ty(context);

	std::vector<Constant*> znode_members;
	znode_members.push_back(ConstantInt::get(int32ty, node->op_type));

#ifdef COMPILED_WITH_CLANG
	std::vector<Constant*> union_anon_members;

	// %union.anon = type [16 x i8]
	char *array = (char*)&node->u;
	for (uint i = 0; i < 16; ++i) {
		union_anon_members.push_back(ConstantInt::get(int8ty, array[i]));
	}

	znode_members.push_back(ConstantArray::get(ArrayType::get(int8ty, 16), union_anon_members));
#else
	// %struct.zend_declarables = type { %struct.zval }
	const StructType* declarables_type = cast<const StructType>(mod->getTypeByName(STRUCT("zend_declarables")));
	std::vector<Constant*> declarables_members;
	declarables_members.push_back(copy_zval(&node->u.constant, mod));

	znode_members.push_back(ConstantStruct::get(declarables_type, declarables_members));
#endif

	return ConstantStruct::get(znode_type, znode_members);
}

static GlobalVariable* dump_opcodes(zend_op* opcodes, int count, Module* mod, ExecutionEngine* engine) {
	const StructType* op_type = cast<const StructType>(mod->getTypeByName(STRUCT("zend_op")));
	const ArrayType *op_array_type = ArrayType::get(op_type, count);

	LLVMContext & context = getGlobalContext();	
	const IntegerType* int8ty = Type::getInt8Ty(context);
	const IntegerType* int32ty = Type::getInt32Ty(context);
	const Type* handler_type = get_function(mod, "phpllvm_get_opcode_handler")->getReturnType();
	std::vector<Constant*> ops;
	for(int i = 0; i < count; i++) {
		// struct _zend_op {
		// 	opcode_handler_t handler;
		// 	znode result;
		// 	znode op1;
		// 	znode op2;
		// 	ulong extended_value;
		// 	uint lineno;
		// 	zend_uchar opcode;
		// };
		// llvm-gcc: %struct.zend_op = type { i32 (%struct.zend_execute_data*)*, %struct.znode, %struct.znode, %struct.znode, i32, i32, i8 }
		// clang:    %struct._zend_op = type <{ i32 (%struct._zend_execute_data*)*, %struct._znode, %struct._znode, %struct._znode, i32, i32, i8, i8, i8, i8 }>


		std::vector<Constant*> op_members;
		
		op_members.push_back(ConstantPointerNull::get(PointerType::getUnqual(handler_type))); // We override this anyway
		op_members.push_back(copy_znode(&opcodes[i].result, mod));
		op_members.push_back(copy_znode(&opcodes[i].op1, mod));
		op_members.push_back(copy_znode(&opcodes[i].op2, mod));
		op_members.push_back(ConstantInt::get(int32ty, opcodes[i].extended_value));
		op_members.push_back(ConstantInt::get(int32ty, opcodes[i].lineno));
		op_members.push_back(ConstantInt::get(int8ty, opcodes[i].opcode));

#ifdef COMPILED_WITH_CLANG
		add_padding(op_members, 3);
#endif

		ops.push_back(ConstantStruct::get(op_type, op_members));
	}

	Constant* initializer = ConstantArray::get(op_array_type, ops);

	GlobalVariable* global_var = new GlobalVariable(op_array_type, true, GlobalValue::InternalLinkage, initializer, "opcodes", mod);

	return global_var;
}

static GlobalVariable* dump_compiled_vars(zend_compiled_variable* vars, int count, Module* mod) {
	LLVMContext & context = getGlobalContext();	
	const IntegerType* int8ty = Type::getInt8Ty(context);
	const IntegerType* int32ty = Type::getInt32Ty(context);
	const StructType* compiled_variable_type = cast<const StructType>(mod->getTypeByName(STRUCT("zend_compiled_variable")));

	std::vector<Constant*> members;

	for(int i = 0; i < count; i++) {
		// typedef struct _zend_compiled_variable {
		// 	char *name;
		// 	int name_len;
		// 	ulong hash_value;
		// } zend_compiled_variable;
		// %struct.zend_compiled_variable = type { i8*, i32, i32 }

		std::vector<Constant*> var_members;

		if (vars[i].name) {
			std::vector<Constant*> zero_indices(2, ConstantInt::get(int32ty, 0));

			Constant* string = ConstantArray::get(vars[i].name);
			GlobalVariable* var = new GlobalVariable(string->getType(), true, GlobalValue::InternalLinkage, string, "compiled_var_name", mod);
			var_members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
		} else
			var_members.push_back(ConstantPointerNull::get(PointerType::getUnqual(int8ty)));

		var_members.push_back(ConstantInt::get(int32ty, vars[i].name_len));
		var_members.push_back(ConstantInt::get(int32ty, vars[i].hash_value));

		members.push_back(ConstantStruct::get(compiled_variable_type, var_members));
	}

	const ArrayType *compiled_var_array_type = ArrayType::get(compiled_variable_type, count);
	Constant* initializer = ConstantArray::get(compiled_var_array_type, members);

	return new GlobalVariable(initializer->getType(), true, GlobalValue::InternalLinkage, initializer, "compiled_vars", mod);
}

static GlobalVariable* dump_brk_cont_array(zend_brk_cont_element* elements, int count, Module* mod) {
	LLVMContext & context = getGlobalContext();	
	const IntegerType* int32ty = Type::getInt32Ty(context);
	const StructType* brk_cont_element_type = cast<const StructType>(mod->getTypeByName(STRUCT("zend_brk_cont_element")));

	std::vector<Constant*> members;

	for(int i = 0; i < count; i++) {
		// typedef struct _zend_brk_cont_element {
		// 	int start;
		// 	int cont;
		// 	int brk;
		// 	int parent;
		// } zend_brk_cont_element;
		// %struct.zend_brk_cont_element = type { i32, i32, i32, i32 }

		std::vector<Constant*> element_members;

		element_members.push_back(ConstantInt::get(int32ty, elements[i].start));
		element_members.push_back(ConstantInt::get(int32ty, elements[i].cont));
		element_members.push_back(ConstantInt::get(int32ty, elements[i].brk));
		element_members.push_back(ConstantInt::get(int32ty, elements[i].parent));

		members.push_back(ConstantStruct::get(brk_cont_element_type, element_members));
	}

	const ArrayType *brk_cont_array_type = ArrayType::get(brk_cont_element_type, count);
	Constant* initializer = ConstantArray::get(brk_cont_array_type, members);

	return new GlobalVariable(initializer->getType(), true, GlobalValue::InternalLinkage, initializer, "brk_cont_arr", mod);
}

static GlobalVariable* dump_try_catch_array(zend_try_catch_element* elements, int count, Module* mod) {
	const StructType* try_catch_element_type = cast<const StructType>(mod->getTypeByName(STRUCT("zend_label")));
	LLVMContext & context = getGlobalContext();	
	const Type* uint_type = Type::getInt32Ty(context); // TODO: adjust this automatically

	std::vector<Constant*> members;

	for (int i = 0; i < count; ++i) {
		// typedef struct _zend_try_catch_element {
		// 	zend_uint try_op;
		// 	zend_uint catch_op;  /* ketchup! */
		// } zend_try_catch_element;
		// %struct.zend_try_catch_element = type { i32, i32 }

		std::vector<Constant*> element_members;

		element_members.push_back(ConstantInt::get(uint_type, elements[i].try_op));
		element_members.push_back(ConstantInt::get(uint_type, elements[i].catch_op));

		members.push_back(ConstantStruct::get(try_catch_element_type, element_members));
	}

	const ArrayType *try_catch_array_type = ArrayType::get(try_catch_element_type, count);
	Constant* initializer = ConstantArray::get(try_catch_array_type, members);

	return new GlobalVariable(initializer->getType(), true, GlobalValue::InternalLinkage, initializer, "try_catch_arr", mod);
}

static GlobalVariable* dump_static_variables(HashTable* table, Module* mod) {
	const Type* hashtable_type = mod->getTypeByName(STRUCT("HashTable"));

	Constant* initializer = Constant::getNullValue(hashtable_type);

	return new GlobalVariable(initializer->getType(), true, GlobalValue::InternalLinkage, initializer, "static_vars", mod);
}

static GlobalVariable* dump_op_array(zend_op_array* op_array, Module* mod, ExecutionEngine* engine) {
	LLVMContext & context = getGlobalContext();	
	const StructType* op_array_type = cast<const StructType>(mod->getTypeByName(STRUCT("zend_op_array")));
	const Type* arg_info_type = mod->getTypeByName(STRUCT("zend_arg_info"));
	const Type* brk_cont_element_type = mod->getTypeByName(STRUCT("zend_brk_cont_element"));
	const Type* class_entry_type = mod->getTypeByName(STRUCT("zend_class_entry"));
	const Type* compiled_variable_type = mod->getTypeByName(STRUCT("zend_compiled_variable"));
	const Type* function_type = mod->getTypeByName(UNION("zend_function"));
	const Type* hashtable_type = mod->getTypeByName(STRUCT("hashtable"));
	const Type* label_type = mod->getTypeByName(STRUCT("zend_label"));
	const Type* op_type = mod->getTypeByName(STRUCT("zend_op"));
	const Type* uint_type = Type::getInt32Ty(context); // TODO: adjust this automatically
	const IntegerType* int8ty = Type::getInt8Ty(context);
	const IntegerType* int32ty = Type::getInt32Ty(context);

	std::vector<Constant*> members;

	std::vector<Constant*> zero_indices(2, ConstantInt::get(int32ty, 0)); // used by getGetElementPointer()

	// zend_uchar type;
	members.push_back(ConstantInt::get(int8ty, op_array->type));
	add_padding(members, 3);

	// char *function_name;
	if (op_array->function_name) {
		Constant* string = ConstantArray::get(op_array->function_name);
		GlobalVariable* var = new GlobalVariable(string->getType(), true, GlobalValue::InternalLinkage, string, "function_name", mod);
		members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(int8ty)));
	}

	// zend_class_entry *scope;
	if (op_array->scope) {
		members.push_back(dump_class_entry(op_array->scope, mod));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(class_entry_type)));
	}

	// zend_uint fn_flags;
	members.push_back(ConstantInt::get(uint_type, op_array->fn_flags));

	// union _zend_function *prototype;
	if (op_array->prototype) {
		members.push_back(dump_function(op_array->prototype, mod));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(function_type)));
	}

	// zend_uint num_args;
	members.push_back(ConstantInt::get(uint_type, op_array->num_args));

	// zend_uint required_num_args;
	members.push_back(ConstantInt::get(uint_type, op_array->num_args));

	// zend_arg_info *arg_info;
	if (op_array->arg_info) {
		GlobalVariable* var = dump_arg_info_array(op_array->arg_info, op_array->num_args, mod);
		members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(arg_info_type)));
	}

	// zend_bool pass_rest_by_reference;
	members.push_back(ConstantInt::get(int8ty, op_array->pass_rest_by_reference));

	// unsigned char return_reference;
	members.push_back(ConstantInt::get(int8ty, op_array->return_reference));

	// zend_bool done_pass_two;
	members.push_back(ConstantInt::get(int8ty, op_array->done_pass_two));

	add_padding(members, 1);

	// zend_uint *refcount;
	if (op_array->refcount) {
		GlobalVariable* var = new GlobalVariable(uint_type, true, GlobalValue::InternalLinkage, ConstantInt::get(uint_type, *op_array->refcount), "refcount", mod);
		members.push_back(var);
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(uint_type)));
	}

	// zend_op *opcodes;
	GlobalVariable* opcodes = dump_opcodes(op_array->opcodes, op_array->last, mod, engine);
	members.push_back(opcodes);

	// zend_uint last, size;
	members.push_back(ConstantInt::get(uint_type, op_array->last));
	members.push_back(ConstantInt::get(uint_type, op_array->last)); // not "size" intentionally

	// zend_compiled_variable *vars;
	if (op_array->vars) {
		GlobalVariable* var = dump_compiled_vars(op_array->vars, op_array->last_var, mod);
		members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(compiled_variable_type)));
	}

	// int last_var, size_var;
	members.push_back(ConstantInt::get(int32ty, op_array->last_var));
	members.push_back(ConstantInt::get(int32ty, op_array->size_var));

	// zend_uint T;
	members.push_back(ConstantInt::get(uint_type, op_array->T));

	// zend_brk_cont_element *brk_cont_array;
	if (op_array->brk_cont_array) {
		GlobalVariable* var = dump_brk_cont_array(op_array->brk_cont_array, op_array->last_brk_cont, mod);
		members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(brk_cont_element_type)));
	}

	// int last_brk_cont;
	members.push_back(ConstantInt::get(int32ty, op_array->last_brk_cont));

	// int current_brk_cont;
	members.push_back(ConstantInt::get(int32ty, op_array->current_brk_cont));

	// zend_try_catch_element *try_catch_array;
	if (op_array->try_catch_array) {
		GlobalVariable* var = dump_try_catch_array(op_array->try_catch_array, op_array->last_try_catch, mod);
		members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(label_type)));
	}

	// int last_try_catch;
	members.push_back(ConstantInt::get(int32ty, op_array->last_try_catch));

	// HashTable *static_variables;
	// members.push_back(dump_static_variables(op_array->static_variables, mod));
	members.push_back(ConstantPointerNull::get(PointerType::getUnqual(hashtable_type)));

	// zend_op *start_op;
	if (op_array->start_op) {
		int start = op_array->start_op - op_array->opcodes;
		std::vector<Constant*> indices;
		indices.push_back(ConstantInt::get(int32ty, 0));
		indices.push_back(ConstantInt::get(int32ty, start));
		members.push_back(ConstantExpr::getGetElementPtr(opcodes, &indices[0], 2));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(op_type)));
	}

	// int backpatch_count;
	members.push_back(ConstantInt::get(int32ty, op_array->backpatch_count));

	// zend_uint this_var;
	members.push_back(ConstantInt::get(uint_type, op_array->this_var));

	// char *filename;
	if (op_array->filename) {
		Constant* string = ConstantArray::get(op_array->filename);
		GlobalVariable* var = new GlobalVariable(string->getType(), true, GlobalValue::InternalLinkage, string, "filename", mod);
		members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(int8ty)));
	}

	// zend_uint line_start;
	members.push_back(ConstantInt::get(uint_type, op_array->line_start));

	// zend_uint line_end;
	members.push_back(ConstantInt::get(uint_type, op_array->line_end));

	// char *doc_comment;
	if (op_array->doc_comment) {
		Constant* string = ConstantArray::get(op_array->doc_comment);
		GlobalVariable* var = new GlobalVariable(string->getType(), true, GlobalValue::InternalLinkage, string, "doc_comment", mod);
		members.push_back(ConstantExpr::getGetElementPtr(var, &zero_indices[0], 2));
	} else {
		members.push_back(ConstantPointerNull::get(PointerType::getUnqual(int8ty)));
	}

	// zend_uint doc_comment_len;
	members.push_back(ConstantInt::get(uint_type, op_array->doc_comment_len));

	// zend_uint early_binding; /* the linked list of delayed declarations */
	members.push_back(ConstantInt::get(uint_type, op_array->early_binding));

	// void *reserved[ZEND_MAX_RESERVED_RESOURCES];
	const ArrayType *ptr_array_type = ArrayType::get(PointerType::getUnqual(int8ty), ZEND_MAX_RESERVED_RESOURCES);
	std::vector<Constant*> vals;
	for(unsigned i = 0; op_array->reserved[i] && i < ZEND_MAX_RESERVED_RESOURCES; i++) {
		// TODO: Do these need to be deep copied?
		GlobalVariable* var = new GlobalVariable(int8ty, true, GlobalValue::InternalLinkage, ConstantInt::get(int8ty, (uint64_t) op_array->reserved[i]), "reserved_ptr", mod);
		vals.push_back(var);
	}
	members.push_back(ConstantArray::get(ptr_array_type, vals));


	GlobalVariable* result = new GlobalVariable(op_array_type, true, GlobalValue::InternalLinkage, ConstantStruct::get(op_array_type, members), "reserved_ptr", mod);

	/* Fix "absolute" jump addreses. */
	Function* fix_jumps = mod->getFunction("phpllvm_fix_jumps");

	std::vector<GenericValue> args;
	GenericValue val;

	val.PointerVal = engine->getPointerToGlobal(result);
	args.push_back(val);

	val.PointerVal = op_array->opcodes;
	args.push_back(val);

	engine->runFunction(fix_jumps, args);


	return result;
}
#endif
