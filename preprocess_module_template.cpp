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

#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Type.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Support/system_error.h>
#include <llvm/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <fstream>
#include <stdlib.h>

extern "C" {
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "phpllvm.h"
}

using namespace llvm;


int main(int argc, char **argv)
{
	Module* mod;
	const char* compile_file = (argc >= 2) ? argv[1] : "module_template.bc";
	int pass = (argc >= 3) ? atoi(argv[2]) : 1;

	OwningPtr<MemoryBuffer> buf;
	error_code code;
	std::string message;
	LLVMContext & context = getGlobalContext();

	/* Read the module. */

	code = MemoryBuffer::getFile(compile_file, buf);
	if (code) {
		fprintf(stderr, "Couldn't read the '%s' file: %s\n", 
			compile_file, code.message().c_str());
		return -1;
	}

	if (!(mod = ParseBitcodeFile(buf.get(), context, &message))) {
		fprintf(stderr, "Couldn't parse the '%s' file: %s\n", 
			compile_file, message.c_str());
		return -1;
	}

	// on the second pass we just want to make all symbols external
	// this is required if running both the JIT engine and the inline optimization
	// otherwise stub'ed internal functions may be removed by the inline optimization
	// and then bad things happen (dangling pointers)
	if (pass == 2) {
		for (Module::iterator I = mod->begin(), E = mod->end(); I != E; ++I) {
			Function *Fn = &*I;
			if (Fn->hasInternalLinkage()) {
				Fn->setLinkage(GlobalValue::ExternalLinkage);
			}
		}

	} else /* if (pass == 1) */ {
		// Preprocessing: set of minor hacks to the Zend code

		const char *global_vars[] = {
#ifndef ZTS
			"executor_globals",
			"compiler_globals",
#endif
			"default_exception_ce",
			"empty_fcall_info",
			"empty_fcall_info_cache",
			"error_exception_ce",
			"zend_compile_file",
			"zend_compile_string",
			"zend_execute",
			"zend_execute_internal",
			"zend_opcode_handlers",
			"zend_throw_exception_hook",
		};

		// transform global variables into an extern reference, so that the bitcode
		// references the VM state and not its own vars
		for (uint i = 0; i < sizeof(global_vars)/sizeof(*global_vars); ++i) {
			GlobalVariable* GV = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal(global_vars[i]));
			if (!GV) continue; // some Zend versions might not have all the symbols
			GV->setInitializer(NULL); // remove 'zeroinitializer'
			GV->setLinkage(GlobalValue::ExternalLinkage);
		}


		// alias zend_error_noreturn() to zend_error()
		// this is needed because zend_error_noreturn() is defined in zend.c,
		// which we don't currently compile to bitcode
		if (Function *aliasFn = mod->getFunction("zend_error_noreturn")) {
			Function *aliasee = mod->getFunction("zend_error");
			GlobalAlias *alias = new GlobalAlias(aliasee->getType(), Function::ExternalLinkage, "zend_error_noreturn", aliasee, mod);
			aliasFn->replaceAllUsesWith(alias);
			alias->takeName(mod->getFunction("zend_error_noreturn"));
		}
	}

	// Write out the module
	verifyModule(*mod, AbortProcessAction);

	raw_fd_ostream bc_os(compile_file, message);
	if (message.size() != 0) {
		fprintf(stderr, "failed opening %s for writing the bitcode: %s\n", 
			compile_file, message.c_str());
		return 1;
	}

	WriteBitcodeToFile(mod, bc_os);

	return 0;
}
