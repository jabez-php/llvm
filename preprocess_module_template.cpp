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

#include <fstream>

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
	const char* compile_file = (argc == 2)? argv[1] : "module_template.bc";

	/* Read the module. */
	MemoryBuffer* buf;
	std::string err;

	if (!(buf = MemoryBuffer::getFile(compile_file, &err))) {
		fprintf(stderr, "Couldn't read the '%s' file: %s\n", compile_file, err.c_str());
		return -1;
	}

	if (!(mod = ParseBitcodeFile(buf, &err))) {
		fprintf(stderr, "Couldn't parse the '%s' file: %s\n", compile_file, err.c_str());
		return -1;
	}

	/* Preprocessing */
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
	for (uint i = 0; i < sizeof(global_vars)/sizeof(global_vars); ++i) {
		GlobalVariable* GV = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal(global_vars[i]));
		GV->setInitializer(NULL); // remove 'zeroinitializer'
		GV->setLinkage(GlobalValue::ExternalLinkage);
	}


// NOTE: keep this #if in sync with zend.h
#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && !defined(DARWIN) && !defined(__hpux) && !defined(_AIX) && !defined(__osf__)
	// alias zend_error_noreturn() to zend_error()
	// this is needed because zend_error_noreturn() is defined in zend.c,
	// which we don't currently compile to bitcode
	Function *aliasee = mod->getFunction("zend_error");
	GlobalAlias *alias = new GlobalAlias(aliasee->getType(), Function::ExternalLinkage, "zend_error_noreturn", aliasee, mod);
	mod->getFunction("zend_error_noreturn")->replaceAllUsesWith(alias);
	alias->takeName(mod->getFunction("zend_error_noreturn"));
#endif

	/* Write out the module */
	verifyModule(*mod, PrintMessageAction);

	std::filebuf fb;
	fb.open(compile_file, std::ios::out);
	std::ostream c_os(&fb);

	WriteBitcodeToFile(mod, c_os);

	fb.close();

	return 0;
}
