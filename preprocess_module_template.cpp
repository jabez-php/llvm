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

int main(int argc, char**argv) {
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
#ifndef ZTS
	GlobalVariable* executor_globals = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("executor_globals"));
	executor_globals->setInitializer(NULL); // remove 'zeroinitializer'
	executor_globals->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* compiler_globals = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("compiler_globals"));
	compiler_globals->setInitializer(NULL); // remove 'zeroinitializer'
	compiler_globals->setLinkage(GlobalValue::ExternalLinkage);
#endif

	GlobalVariable* zend_execute = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_execute"));
	zend_execute->setInitializer(NULL); // remove 'zeroinitializer'
	zend_execute->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* zend_compile_string = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_compile_string"));
	zend_compile_string->setInitializer(NULL); // remove 'zeroinitializer'
	zend_compile_string->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* zend_compile_file = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_compile_file"));
	zend_compile_file->setInitializer(NULL); // remove 'zeroinitializer'
	zend_compile_file->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* zend_execute_internal = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_execute_internal"));
	zend_execute_internal->setInitializer(NULL); // remove 'zeroinitializer'
	zend_execute_internal->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* zend_opcode_handlers = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_opcode_handlers"));
	zend_opcode_handlers->setInitializer(NULL); // remove 'zeroinitializer'
	zend_opcode_handlers->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* zend_throw_exception_hook = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_throw_exception_hook"));
	zend_throw_exception_hook->setInitializer(NULL); // remove 'zeroinitializer'
	zend_throw_exception_hook->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* error_exception_ce = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("error_exception_ce"));
	error_exception_ce->setInitializer(NULL); // remove 'zeroinitializer'
	error_exception_ce->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* default_exception_ce = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("default_exception_ce"));
	default_exception_ce->setInitializer(NULL); // remove 'zeroinitializer'
	default_exception_ce->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* empty_fcall_info = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("empty_fcall_info"));
	empty_fcall_info->setInitializer(NULL); // remove 'zeroinitializer'
	empty_fcall_info->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* empty_fcall_info_cache = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("empty_fcall_info_cache"));
	empty_fcall_info_cache->setInitializer(NULL); // remove 'zeroinitializer'
	empty_fcall_info_cache->setLinkage(GlobalValue::ExternalLinkage);

// NOTE: keep this #if in sync with zend.h
#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && !defined(DARWIN) && !defined(__hpux) && !defined(_AIX) && !defined(__osf__)
	Function *aliasee = mod->getFunction("zend_error");
	GlobalAlias *alias = new GlobalAlias(aliasee->getType(), Function::ExternalLinkage, "zend_error_noreturn", aliasee, mod);
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
