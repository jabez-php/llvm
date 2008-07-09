#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Type.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Assembly/PrintModulePass.h>

#include <fstream>

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
	GlobalVariable* executor_globals = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("executor_globals"));
	executor_globals->setInitializer(NULL); // remove 'zeroinitializer'
	executor_globals->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* compiler_globals = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("executor_globals"));
	compiler_globals->setInitializer(NULL); // remove 'zeroinitializer'
	compiler_globals->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* zend_execute = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_execute"));
	zend_execute->setInitializer(NULL); // remove 'zeroinitializer'
	zend_execute->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* zend_compile_string = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_compile_string"));
	zend_compile_string->setInitializer(NULL); // remove 'zeroinitializer'
	zend_compile_string->setLinkage(GlobalValue::ExternalLinkage);

	GlobalVariable* zend_compile_file = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("zend_compile_file"));
	zend_compile_file->setInitializer(NULL); // remove 'zeroinitializer'
	zend_compile_file->setLinkage(GlobalValue::ExternalLinkage);

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
