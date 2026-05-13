include ../Makefile.kernel

P2_TEST_SUBDIRS = tests/userprog tests/filesys/base tests/userprog/no-vm tests/threads
P2_USER_TEST_SUBDIRS = tests/userprog tests/threads
P2_FORK_DEPENDENT_TESTS = tests/userprog/fork-once
P2_FORK_DEPENDENT_TESTS += tests/userprog/fork-multiple
P2_FORK_DEPENDENT_TESTS += tests/userprog/fork-recursive
P2_FORK_DEPENDENT_TESTS += tests/userprog/fork-read
P2_FORK_DEPENDENT_TESTS += tests/userprog/fork-close
P2_FORK_DEPENDENT_TESTS += tests/userprog/fork-boundary
P2_FORK_DEPENDENT_TESTS += tests/userprog/wait-simple
P2_FORK_DEPENDENT_TESTS += tests/userprog/wait-twice
P2_FORK_DEPENDENT_TESTS += tests/userprog/wait-killed
P2_FORK_DEPENDENT_TESTS += tests/userprog/exec-boundary
P2_FORK_DEPENDENT_TESTS += tests/userprog/exec-read
P2_FORK_DEPENDENT_TESTS += tests/userprog/multi-recurse
P2_FORK_DEPENDENT_TESTS += tests/userprog/multi-child-fd
P2_FORK_DEPENDENT_TESTS += tests/userprog/rox-child
P2_FORK_DEPENDENT_TESTS += tests/userprog/rox-multichild
P2_FORK_DEPENDENT_TESTS += tests/userprog/no-vm/multi-oom
P2_EXEC_BASIC_TESTS = tests/userprog/exec-once tests/userprog/exec-arg
P2_FORK_TEST ?= fork-once
P2_FORK_TEST_PATH = $(if $(findstring /,$(P2_FORK_TEST)),$(P2_FORK_TEST),$(if $(filter multi-oom,$(P2_FORK_TEST)),tests/userprog/no-vm/$(P2_FORK_TEST),tests/userprog/$(P2_FORK_TEST)))
P2_DIRS = $(sort $(addprefix build/,$(KERNEL_SUBDIRS) $(P2_TEST_SUBDIRS) lib/user))
P2_USER_DIRS = $(sort $(addprefix build/,$(KERNEL_SUBDIRS) $(P2_USER_TEST_SUBDIRS) lib/user))

.PHONY: p2-nofork-check p2-nofork-results p2-user-nofork-check p2-user-nofork-results p2-exec-basic-check p2-exec-basic-results p2-fork-check p2-fork-results p2-fork-one p2-fork-one-result p2-fork-one-output
P2_DIRS = $(sort $(addprefix build/,$(KERNEL_SUBDIRS) $(P2_TEST_SUBDIRS) lib/user))
P2_USER_DIRS = $(sort $(addprefix build/,$(KERNEL_SUBDIRS) $(P2_USER_TEST_SUBDIRS) lib/user))

.PHONY: p2-nofork-check p2-nofork-results p2-user-nofork-check p2-user-nofork-results p2-pointer-check p2-pointer-results p2-fork-check p2-fork-results p2-fork-one p2-fork-one-result p2-fork-one-output

p2-nofork-check: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) selected-check TEST_SUBDIRS="$(P2_TEST_SUBDIRS)" EXCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-nofork-results: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) selected-results TEST_SUBDIRS="$(P2_TEST_SUBDIRS)" EXCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-user-nofork-check: $(P2_USER_DIRS) build/Makefile
	cd build && $(MAKE) selected-check TEST_SUBDIRS="$(P2_USER_TEST_SUBDIRS)" EXCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-user-nofork-results: $(P2_USER_DIRS) build/Makefile
	cd build && $(MAKE) selected-results TEST_SUBDIRS="$(P2_USER_TEST_SUBDIRS)" EXCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-exec-basic-check: $(P2_USER_DIRS) build/Makefile
	cd build && $(MAKE) selected-check TEST_SUBDIRS="tests/userprog" INCLUDE_TESTS="$(P2_EXEC_BASIC_TESTS)"

p2-exec-basic-results: $(P2_USER_DIRS) build/Makefile
	cd build && $(MAKE) selected-results TEST_SUBDIRS="tests/userprog" INCLUDE_TESTS="$(P2_EXEC_BASIC_TESTS)"

p2-fork-check: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) selected-check TEST_SUBDIRS="$(P2_TEST_SUBDIRS)" INCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-fork-results: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) selected-results TEST_SUBDIRS="$(P2_TEST_SUBDIRS)" INCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-fork-one: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) selected-check TEST_SUBDIRS="$(P2_TEST_SUBDIRS)" INCLUDE_TESTS="$(P2_FORK_TEST_PATH)"

p2-fork-one-result: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) $(P2_FORK_TEST_PATH).result TEST_SUBDIRS="$(P2_TEST_SUBDIRS)"
	@cat build/$(P2_FORK_TEST_PATH).result

p2-fork-one-output: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) $(P2_FORK_TEST_PATH).output TEST_SUBDIRS="$(P2_TEST_SUBDIRS)"
	@cat build/$(P2_FORK_TEST_PATH).output
