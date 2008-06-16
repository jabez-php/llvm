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

extern "C" {
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "phpllvm.h"
}

#include "phpllvm_compile.h"
#include "phpllvm_execute.h"

using namespace phpllvm;

ZEND_DECLARE_MODULE_GLOBALS(phpllvm)

#define TEMP_FILE "previous_execution.bc"

static function_entry phpllvm_functions[] = {
	{NULL, NULL, NULL}
};

zend_module_entry phpllvm_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	PHP_PHPLLVM_EXTNAME,
	phpllvm_functions,
	PHP_MINIT(phpllvm),
	PHP_MSHUTDOWN(phpllvm),
	PHP_RINIT(phpllvm),
	NULL,
	NULL,
#if ZEND_MODULE_API_NO >= 20010901
	PHP_PHPLLVM_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};

PHP_INI_BEGIN()
	PHP_INI_ENTRY("phpllvm.active", "1", PHP_INI_SYSTEM, NULL)
PHP_INI_END()

#ifdef COMPILE_DL_PHPLLVM
ZEND_GET_MODULE(phpllvm)
#endif

static void phpllvm_init_globals(zend_phpllvm_globals *phpllvm_globals)
{
}

PHP_RINIT_FUNCTION(phpllvm)
{
	if (INI_BOOL("phpllvm.active"))
		override_executor(TSRMLS_C);

	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(phpllvm)
{
	return SUCCESS;
}

PHP_MINIT_FUNCTION(phpllvm)
{
	ZEND_INIT_MODULE_GLOBALS(phpllvm, phpllvm_init_globals, NULL);

	REGISTER_INI_ENTRIES();

	// FILE* temp = fopen(TEMP_FILE, "r");
	// if (temp) {
	// 	init_jit_engine(TEMP_FILE, TSRMLS_C);
	// 	fclose(temp);
	// } else
	init_jit_engine(NULL, TSRMLS_C);

	return SUCCESS;
}


PHP_MSHUTDOWN_FUNCTION(phpllvm)
{
	UNREGISTER_INI_ENTRIES();

	if (INI_BOOL("phpllvm.active"))
		restore_executor(TSRMLS_C);

	save_module(TEMP_FILE, TSRMLS_C);
	destroy_jit_engine(TSRMLS_C);

	return SUCCESS;
}
