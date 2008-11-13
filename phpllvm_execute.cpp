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

#include "phpllvm_execute.h"
#include "phpllvm_compile.h"

#include <llvm/PassManager.h>
#include <llvm/ModuleProvider.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;
using namespace phpllvm;

// pointer to the original Zend engine execute function
typedef void (zend_execute_t)(zend_op_array *op_array TSRMLS_DC);
static zend_execute_t *old_execute = NULL;

static ExecutionEngine* engine;
static Module* module;
static ModuleProvider* provider;
static FunctionPassManager* opt_fpass_manager;
static PassManager pass_manager;

static void optimize_function(Function* function) {
	pass_manager.run(*module);
	opt_fpass_manager->run(*function);

#ifdef DEBUG_PHPLLVM
	verifyFunction(*function, AbortProcessAction);
#endif
}

void phpllvm::save_module(const char* filename) {
#ifdef DEBUG_PHPLLVM
	verifyModule(*module, AbortProcessAction);
#endif

	std::string ErrorInfo;
	raw_fd_ostream os(filename, true, ErrorInfo);

	if (ErrorInfo.empty()) {
		WriteBitcodeToFile(module, os);
	}
}

void phpllvm::init_jit_engine(const char* filename) {

	if (!filename) {
		filename = "module_template.bc";
	}

	// read in the template that includes the handlers
	MemoryBuffer* buf;
	std::string err;

	if (!(buf = MemoryBuffer::getFile(filename, &err))) {
		fprintf(stderr, "Couldn't read handlers file: %s", err.c_str());
	}

	if (!(module = ParseBitcodeFile(buf, &err))) {
		fprintf(stderr, "Couldn't parse handlers file: %s", err.c_str());
	}

	provider = new ExistingModuleProvider(module);
	engine = ExecutionEngine::create(provider);

	// Force codegen of handlers. this is a workaround for an LLVM bug in the JIT engine
	for (Module::iterator I = module->begin(), E = module->end(); I != E; ++I) {
		Function *Fn = &*I;
		if (!Fn->isDeclaration() && Fn->getName().compare(0, 5, "ZEND_") == 0) {
			// fprintf(stderr, "Generating: %s\n", Fn->getName().c_str());
			engine->getPointerToFunction(Fn);
		}
	}

	// Set up the optimization passes
	opt_fpass_manager = new FunctionPassManager(provider);

	opt_fpass_manager->add(new TargetData(*engine->getTargetData()));
	pass_manager.add(new TargetData(*engine->getTargetData()));

	// IPO optimizations
	pass_manager.add(createFunctionInliningPass());

	// local optimizations
	opt_fpass_manager->add(createInstructionCombiningPass());
	opt_fpass_manager->add(createReassociatePass());
	opt_fpass_manager->add(createGVNPass());
	opt_fpass_manager->add(createCFGSimplificationPass());
}

void phpllvm::destroy_jit_engine() {
	delete engine;
	delete opt_fpass_manager;
}

void phpllvm::override_executor() {
	old_execute = zend_execute;
	zend_execute = phpllvm::execute;
}

void phpllvm::restore_executor() {
	if (old_execute) {
		zend_execute = old_execute;
	}
}

void phpllvm::execute(zend_op_array *op_array TSRMLS_DC) {

	/* Get/create the compiled function */

	char* name;
	bool cache;
	Function* function;

	if (!op_array->filename || std::string("Command line code") == op_array->filename) {

		/* Don't cache "Command line code". */
		name = estrdup("command_line_code");
		cache = false;
		function = NULL;

	} else {
		spprintf(&name, 0, "%s__c__%s__f__%s__s__%u",
			(op_array->filename)? op_array->filename : "",
			(op_array->scope)? op_array->scope->name : "",
			(op_array->function_name)? op_array->function_name : "",
			(op_array->start_op)? op_array->start_op - op_array->opcodes : 0);

		cache = true;
		function = module->getFunction(name);

	}

	if (!function) {
		function = compile_op_array(op_array, name, module, engine TSRMLS_CC);

		if (!function) {
			/* Note that we can't even call old_execute because the template includes globals
			 	that are duplicates of the original executor's globals. Hence we can either
				use only old_execute or our execute. */
			zend_error_noreturn(E_ERROR, "Couldn't compile LLVM Function for %s.\n", name);
		}

		optimize_function(function);
	}
	// else fprintf(stderr, "cache hit: %s\n", name);

	// fprintf(stderr, "executing %s\n", name);
	efree(name);

	/* Call the compiled function */
	std::vector<GenericValue> args;
	GenericValue val;

	val.PointerVal = op_array;
	args.push_back(val);

#ifdef ZTS
	val.PointerVal = TSRMLS_C;
	args.push_back(val);
#endif

	engine->runFunction(function, args);

	if(!cache) {
		engine->freeMachineCodeForFunction(function);
		cast<Instruction>(function->use_begin().getUse().getUser())->eraseFromParent(); // delete the 'call' instruction
		function->eraseFromParent();
	}
}
