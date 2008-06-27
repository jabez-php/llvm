LLVM_LINK = $(llvm_bins_path)llvm-link

$(srcdir)/phpllvm_execute.cpp: $(srcdir)/module_template.bc

$(builddir)/preprocess_module_template: $(srcdir)/preprocess_module_template.cpp
	$(CXX) $(CXXFLAGS_CLEAN) $< -o $@ $(LDFLAGS)

$(srcdir)/module_template.bc: phpllvm_runtime_helpers.c $(php_sources_path)/Zend/zend_execute.c $(php_sources_path)/Zend/zend_vm_execute.h $(php_sources_path)/Zend/zend_exceptions.c $(builddir)/preprocess_module_template
	$(LLVM_CC) -emit-llvm $(COMMON_FLAGS) $(CFLAGS_CLEAN) $(EXTRA_CFLAGS) -c $(srcdir)/phpllvm_runtime_helpers.c -o $(builddir)/phpllvm_runtime_helpers.bc.o
	$(LLVM_CC) -emit-llvm $(COMMON_FLAGS) $(CFLAGS_CLEAN) $(EXTRA_CFLAGS) -c $(php_sources_path)/Zend/zend_exceptions.c -o $(builddir)/zend_exceptions.bc.o
	$(LLVM_CC) -emit-llvm $(COMMON_FLAGS) $(CFLAGS_CLEAN) $(EXTRA_CFLAGS) -c $(php_sources_path)/Zend/zend_execute.c -o $(builddir)/zend_execute.bc.o
	$(LLVM_CC) -emit-llvm $(COMMON_FLAGS) $(CFLAGS_CLEAN) $(EXTRA_CFLAGS) -c $(php_sources_path)/Zend/zend_execute_API.c -o $(builddir)/zend_execute_API.bc.o
	$(LLVM_CC) -emit-llvm $(COMMON_FLAGS) $(CFLAGS_CLEAN) $(EXTRA_CFLAGS) -c $(php_sources_path)/Zend/zend_compile.c -o $(builddir)/zend_compile.bc.o
	$(LLVM_LINK) $(builddir)/phpllvm_runtime_helpers.bc.o $(builddir)/zend_exceptions.bc.o $(builddir)/zend_execute.bc.o $(builddir)/zend_execute_API.bc.o $(builddir)/zend_compile.bc.o > module_template.bc
	$(builddir)/preprocess_module_template module_template.bc

$(srcdir)/phpllvm_compile.cpp: $(srcdir)/phpllvm_handler_lookup.h

$(srcdir)/phpllvm_handler_lookup.h: $(php_sources_path)/Zend/zend_vm_execute.h $(srcdir)/generate_handler_lookup.php
	php $(srcdir)/generate_handler_lookup.php < $(php_sources_path)/Zend/zend_vm_execute.h
