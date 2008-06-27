#include <llvm/Module.h>
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
	buf = MemoryBuffer::getFile(compile_file);

	mod = ParseBitcodeFile(buf);

	/* Preprocessing */
	GlobalVariable* executor_globals = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("executor_globals"));
	GlobalVariable* compiler_globals = dynamic_cast<GlobalVariable*>(mod->getNamedGlobal("executor_globals"));

	executor_globals->setInitializer(NULL); // remove 'zeroinitializer'
	executor_globals->setLinkage(GlobalValue::ExternalLinkage);

	compiler_globals->setInitializer(NULL); // remove 'zeroinitializer'
	compiler_globals->setLinkage(GlobalValue::ExternalLinkage);

	/* Write out the module */
	verifyModule(*mod, PrintMessageAction);

	std::filebuf fb;
	fb.open(compile_file, std::ios::out);
	std::ostream c_os(&fb);
	
	WriteBitcodeToFile(mod, c_os);
	
	fb.close();

    return 0;
}