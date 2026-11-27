# the reference pages of the docs (docs/reference), made from the doc
# comments of the headers, the system call table and the ioctl numbers

.PHONY: docs docs-check

DOCS_TOOL := tools/docs/make_reference.py
DOCS_CHECK_DIR := $(BUILD_DIR)/docs-check

docs:
	$(PYTHON) -I $(DOCS_TOOL)

# fails when docs/reference is not what the source makes (run make docs)
docs-check:
	@rm -rf $(DOCS_CHECK_DIR)
	@$(PYTHON) -I $(DOCS_TOOL) --out $(DOCS_CHECK_DIR)
	@diff -r -q docs/reference $(DOCS_CHECK_DIR) >/dev/null || { \
		diff -r -q docs/reference $(DOCS_CHECK_DIR); \
		echo "docs/reference is out of date: run make docs and commit the result"; \
		exit 1; }
	@echo "docs/reference is up to date"
