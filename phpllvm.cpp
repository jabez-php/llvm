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

extern "C" {
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"
#include "php_ini.h"
#include "phpllvm.h"
}

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include "llvm/Config/config.h"

#include "phpllvm_compile.h"
#include "phpllvm_execute.h"

using namespace phpllvm;

ZEND_DECLARE_MODULE_GLOBALS(phpllvm)

#define TEMP_FILE "previous_execution.bc"

PHP_INI_BEGIN()
	PHP_INI_ENTRY("phpllvm.active", "1", PHP_INI_SYSTEM, NULL)
PHP_INI_END()

#ifdef COMPILE_DL_PHPLLVM
ZEND_GET_MODULE(phpllvm)
#endif

static void phpllvm_init_globals(zend_phpllvm_globals *phpllvm_globals)
{
}

static PHP_MINIT_FUNCTION(phpllvm)
{
	ZEND_INIT_MODULE_GLOBALS(phpllvm, phpllvm_init_globals, NULL);

	REGISTER_INI_ENTRIES();

	init_jit_engine(NULL);

	if (INI_BOOL("phpllvm.active"))
		override_executor();

	return SUCCESS;
}


static PHP_MSHUTDOWN_FUNCTION(phpllvm)
{
	if (INI_BOOL("phpllvm.active"))
		restore_executor();

	UNREGISTER_INI_ENTRIES();

	save_module(TEMP_FILE);
	destroy_jit_engine();

	return SUCCESS;
}

static const char* getLLVMString()
{
#ifdef LLVM_VERSION_INFO
    return PACKAGE_VERSION ", " LLVM_VERSION_INFO;
#else
    return PACKAGE_VERSION;
#endif
}

static PHP_MINFO_FUNCTION(phpllvm)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "Revision", "$Revision$");
	php_info_print_table_row(2, "LLVM version", getLLVMString());
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

static zend_function_entry phpllvm_functions[] = {
	{NULL, NULL, NULL}
};

zend_module_entry phpllvm_module_entry = {
	STANDARD_MODULE_HEADER,
	PHP_PHPLLVM_EXTNAME,
	phpllvm_functions,
	PHP_MINIT(phpllvm),
	PHP_MSHUTDOWN(phpllvm),
	NULL,
	NULL,
	PHP_MINFO(phpllvm),
	PHP_PHPLLVM_VERSION,
	STANDARD_MODULE_PROPERTIES
};
