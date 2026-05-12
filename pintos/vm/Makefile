include ../Makefile.kernel

P2_TEST_SUBDIRS = tests/userprog tests/filesys/base tests/userprog/no-vm tests/threads
P2_USER_TEST_SUBDIRS = tests/userprog tests/threads
P2_FORK_DEPENDENT_TESTS = tests/userprog/fork-%
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
P2_POINTER_TESTS = tests/userprog/create-bad-ptr
P2_POINTER_TESTS += tests/userprog/create-bound
P2_POINTER_TESTS += tests/userprog/open-bad-ptr
P2_POINTER_TESTS += tests/userprog/open-boundary
P2_POINTER_TESTS += tests/userprog/read-bad-ptr
P2_POINTER_TESTS += tests/userprog/read-boundary
P2_POINTER_TESTS += tests/userprog/write-bad-ptr
P2_POINTER_TESTS += tests/userprog/write-boundary
P2_POINTER_TESTS += tests/userprog/exec-bad-ptr
P2_POINTER_TESTS += tests/userprog/bad-read
P2_POINTER_TESTS += tests/userprog/bad-write
P2_POINTER_TESTS += tests/userprog/bad-read2
P2_POINTER_TESTS += tests/userprog/bad-write2
P2_POINTER_TESTS += tests/userprog/bad-jump
P2_POINTER_TESTS += tests/userprog/bad-jump2
P2_DIRS = $(sort $(addprefix build/,$(KERNEL_SUBDIRS) $(P2_TEST_SUBDIRS) lib/user))
P2_USER_DIRS = $(sort $(addprefix build/,$(KERNEL_SUBDIRS) $(P2_USER_TEST_SUBDIRS) lib/user))

.PHONY: p2-nofork-check p2-nofork-results p2-user-nofork-check p2-user-nofork-results p2-pointer-check p2-pointer-results

p2-nofork-check: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) selected-check TEST_SUBDIRS="$(P2_TEST_SUBDIRS)" EXCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-nofork-results: $(P2_DIRS) build/Makefile
	cd build && $(MAKE) selected-results TEST_SUBDIRS="$(P2_TEST_SUBDIRS)" EXCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-user-nofork-check: $(P2_USER_DIRS) build/Makefile
	cd build && $(MAKE) selected-check TEST_SUBDIRS="$(P2_USER_TEST_SUBDIRS)" EXCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-user-nofork-results: $(P2_USER_DIRS) build/Makefile
	cd build && $(MAKE) selected-results TEST_SUBDIRS="$(P2_USER_TEST_SUBDIRS)" EXCLUDE_TESTS="$(P2_FORK_DEPENDENT_TESTS)"

p2-pointer-check: $(P2_USER_DIRS) build/Makefile
	cd build && $(MAKE) selected-check TEST_SUBDIRS="$(P2_USER_TEST_SUBDIRS)" INCLUDE_TESTS="$(P2_POINTER_TESTS)"

p2-pointer-results: $(P2_USER_DIRS) build/Makefile
	cd build && $(MAKE) selected-results TEST_SUBDIRS="$(P2_USER_TEST_SUBDIRS)" INCLUDE_TESTS="$(P2_POINTER_TESTS)"
