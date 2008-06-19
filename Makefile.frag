LLVM_LINK = $(llvm_bins_path)llvm-link

$(srcdir)/phpllvm_execute.cpp: $(srcdir)/module_template.bc

$(srcdir)/module_template.bc: phpllvm_runtime_helpers.c $(php_sources_path)/Zend/zend_execute.c $(php_sources_path)/Zend/zend_exceptions.c
	$(LLVM_CC) -emit-llvm $(COMMON_FLAGS) $(CFLAGS_CLEAN) $(EXTRA_CFLAGS) -c $(php_sources_path)/Zend/zend_exceptions.c -o $(builddir)/zend_exceptions.bc.o
	$(LLVM_CC) -emit-llvm $(COMMON_FLAGS) $(CFLAGS_CLEAN) $(EXTRA_CFLAGS) -c $(php_sources_path)/Zend/zend_execute.c -o $(builddir)/zend_execute.bc.o
	$(LLVM_CC) -emit-llvm $(COMMON_FLAGS) $(CFLAGS_CLEAN) $(EXTRA_CFLAGS) -c $(srcdir)/phpllvm_runtime_helpers.c -o $(builddir)/phpllvm_runtime_helpers.bc.o
	$(LLVM_LINK) $(builddir)/phpllvm_runtime_helpers.bc.o $(builddir)/zend_execute.bc.o $(builddir)/zend_exceptions.bc.o > module_template.bc

$(srcdir)/phpllvm_compile.cpp: $(srcdir)/phpllvm_handler_lookup.h

$(srcdir)/phpllvm_handler_lookup.h: $(php_sources_path)/Zend/zend_vm_execute.h
	php $(srcdir)/generate_handler_lookup.php < $(php_sources_path)/Zend/zend_vm_execute.h
