# Pintos Review Checklist

Use this only when Pintos mode is active. All examples must be in C. Do not use Java, Python, or unrelated language analogies. Explain concurrency with Pintos primitives such as `lock_acquire`, `lock_release`, `sema_down`, `sema_up`, `cond_wait`, `intr_disable`, and `thread_block`. Keep the tone educational: explain why the pattern is risky, not just that it is wrong. When possible, mention likely `make check` or grading-test impact.

KAIST CS330 uses 64-bit Pintos. Watch for stale 32-bit assumptions in pointer sizes, register widths, syscall argument extraction, stack alignment, and trap frame fields.

## Common Checks For All Phases

Synchronization:

- `lock_acquire` and `lock_release` are paired on every return path.
- Locks are not acquired from interrupt context; check `intr_context()` where relevant.
- Code does not call `thread_block()` directly while holding a lock unless the surrounding primitive requires and proves it safe.
- `sema_down` and `sema_up` are paired and cannot lose wakeups.
- `cond_wait` is called only while holding the associated lock and rechecks the condition after wakeup when needed.
- Interrupt state is restored correctly after `intr_disable()`.

Memory safety:

- `palloc_get_page`/`palloc_free_page` and `malloc`/`free` are paired.
- No use-after-free or dangling pointer remains after cleanup.
- Objects embedded in Pintos lists are removed before being freed.
- Kernel stack stays within 4 KB; avoid large local arrays or structs.
- Every allocation failure path cleans up earlier resources.

Data structures:

- `list_entry(elem, struct type, member)` uses the correct member.
- `list_remove` is not followed by accidental reuse of stale links.
- `ready_list`, `sleep_list`, frame lists, and wait lists preserve their intended ordering.
- Shared lists are protected by the intended lock or interrupt discipline.

Testing and grading:

- Review likely `make check` impact by phase.
- Be suspicious of fixes that hide races with sleeps or timing assumptions.
- Distinguish a general implementation from code tailored only to one visible test.

KAIST 64-bit specifics:

- Syscall arguments should not use stale 32-bit `*(esp + 4)` style extraction.
- x86-64 syscall arguments come through registers/trap frame fields such as `rax`, `rdi`, `rsi`, `rdx`, `r10`, `r8`, and `r9` depending on the local skeleton.
- User stack setup should respect 16-byte alignment requirements.
- Pointer truncation to `int` or `uint32_t` is usually a bug.

## Phase 1: threads

- `thread_yield()` and `thread_block()` are called with the correct interrupt state.
- Alarm clock implementation does not busy wait.
- Priority scheduling uses `ready_list` ordering or `list_max` consistently.
- Priority donation handles nested donation, multiple donors, lock release, and priority restoration.
- Donation does not recurse indefinitely and respects any project-defined depth cap.
- MLFQS fixed-point arithmetic uses the correct scale, rounding, and update cadence.

Likely tests: `alarm-*`, `priority-*`, `priority-donate-*`, `mlfqs-*`.

## Phase 2: userprog

- User pointers are validated with `is_user_vaddr`, page table lookup, `get_user`/`put_user`, or a consistent page-fault strategy.
- Syscall entry validates buffers and strings across page boundaries.
- Argument passing builds a correctly aligned x86-64 user stack.
- File descriptor table logic handles invalid descriptors, repeated close, process exit, and synchronization.
- Parent/child wait and exit state cannot leak or double-free.
- Open files are closed and denied writes are released on process exit.

Likely tests: `args-*`, `sc-*`, `wait-*`, `exec-*`, `multi-*`, file descriptor tests.

## Phase 3: vm

- Supplemental page table, frame table, swap table, and file-backed page locks have a consistent order.
- Page fault handler cannot recursively fault forever.
- Frame eviction pins pages during I/O and avoids evicting unsafe frames.
- Swap slots are freed exactly once.
- Lazy loading validates file offsets and read/zero byte counts.
- `mmap` dirty pages are written back correctly and unmapped pages clean up frames/swap.

Likely tests: `page-*`, `mmap-*`, `swap-*`, `pt-*`, `cow-*` if present.

## Phase 4: filesys

- Buffer cache entries are synchronized per block and with eviction.
- Dirty cache blocks are flushed at the required times.
- Inode growth handles direct, indirect, and doubly indirect block allocation consistently.
- Concurrent inode/file operations cannot corrupt length, deny-write count, or block pointers.
- Subdirectories handle `.`, `..`, root, removal, and current working directory semantics.
- Free-map updates remain consistent on partial allocation failure.

Likely tests: `grow-*`, `dir-*`, `syn-*`, `cache-*`, persistence tests.

## Pintos Diff-Adjacent Checks

- If `threads/thread.h`, `threads/synch.h`, or central headers changed, inspect dependent files.
- If `Makefile.build` changed, verify every new source file is registered once and in the correct phase.
- If Pintos command options or utilities changed, inspect matching `utils/` behavior.
- Search existing `tests/` for the changed function or behavior to avoid breaking hidden assumptions.
