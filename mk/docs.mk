# the reference: Doxygen makes HTML pages from the comments of libc, the
# libraries and the kernel (tools/docs/Doxyfile). the site build runs it
# too; make docs is for a look on this computer.

.PHONY: docs

DOXYGEN ?= doxygen

docs:
	@command -v $(DOXYGEN) >/dev/null 2>&1 || { echo "missing tool: $(DOXYGEN)"; exit 1; }
	@rm -rf build/docs/html && mkdir -p build/docs
	$(DOXYGEN) tools/docs/Doxyfile
	@echo "the reference: build/docs/html/index.html ($$(wc -l < build/docs/warnings.txt | tr -d ' ') warnings in build/docs/warnings.txt)"
