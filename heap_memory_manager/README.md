<div align="center">

# Hmm - Heap Memory Manager

**A custom `malloc` / `free` / `calloc` / `realloc` for Linux, written in C**

Best-fit allocation · boundary-tag coalescing · 16-byte alignment · `sbrk()`-based heap

![language](https://img.shields.io/badge/language-C%20(GNU)-2F6DB5)
![platform](https://img.shields.io/badge/platform-Linux-1B365D)
![build](https://img.shields.io/badge/build-GNU%20Make-5F6B7A)
![output](https://img.shields.io/badge/output-libhmm.a%20%7C%20libhmm.so-2F6DB5)

</div>

---

## Contents

1. [Datasheet](#1-datasheet)
2. [Overview](#2-overview)
3. [Source files](#3-source-files)
4. [Memory model](#4-memory-model)
5. [API reference](#5-api-reference)
6. [How it works](#6-how-it-works)
7. [Build manual](#7-build-manual)
8. [Using the library](#8-using-the-library)
9. [Worked example](#9-worked-example)
10. [Limitations and notes](#10-limitations-and-notes)
11. [Troubleshooting](#11-troubleshooting)
12. [Flowcharts](#12-flowcharts)

---

## 1. Datasheet

| Parameter | Value |
|---|---|
| Language / compiler | GNU C, built with `gcc` (`-Wall -Wextra -g -fPIC`) |
| Platform | Linux (uses `sbrk()`) |
| Replaces | `malloc`, `free`, `calloc`, `realloc` |
| Placement policy | **Best fit** (size-sorted free list, first match is the smallest fit) |
| Free-list structure | Doubly linked, sorted by ascending block size |
| Coalescing | Immediate, with both physical neighbours (boundary tags) |
| Alignment | 16 bytes (the pointer returned to the caller) |
| Header size | 8 bytes (`size` + 2 flag bits) |
| Footer size | 8 bytes (free blocks only) |
| Minimum block | 32 bytes (24 usable bytes) |
| Block size granularity | 16 bytes |
| Heap growth step | at least 128 KiB (`MIN_INCREMENT`) |
| Heap shrink threshold | free top block of 128 KiB or more (`MIN_DECREMENT`) |
| Static library | `build/lib/libhmm.a` |
| Shared library | `build/lib/libhmm.so` |
| Thread safety | One global `pthread` mutex (`heap_lock`) held for the whole of `HmmAlloc` and `HmmFree` |
| Global state | `head` (free-list head), `heap_end` (top of managed heap), `heap_lock` (mutex) |

**Size of a block for a request `n`:** `ALIGN(n + 8)`, but never less than 32.

| Request `n` | Block size | Usable bytes (`hmmSize`) |
|---:|---:|---:|
| 1 - 24 | 32 | 24 |
| 25 - 40 | 48 | 40 |
| 50 | 64 | 56 |
| 100 | 112 | 104 |
| 200 | 208 | 200 |
| 500 | 512 | 504 |

---

## 2. Overview

Hmm is a small general-purpose heap allocator. It asks the kernel for memory with `sbrk()`, cuts that memory into blocks, and hands the blocks out through the standard allocator functions.

* **Allocation** looks for the smallest free block that fits. If none does, the heap grows by at least 128 KiB. A block that is larger than needed is split, and the remainder goes back to the free list.
* **Freeing** merges the block with any free neighbour, so free space does not break into small pieces. When the freed block is the top block of the heap and is at least 128 KiB, it is returned to the kernel with `sbrk(-n)`.
* **Wrappers** in `heapWrappers.c` define the real `malloc`, `free`, `calloc` and `realloc` symbols. Linking `libhmm` into a program, or preloading `libhmm.so`, replaces the system allocator for that process.

---

## 3. Source files

```
.
├── Makefile
├── inc/
│   └── Hmm.h             block metadata, constants, inline helpers, prototypes
├── src/
│   ├── HmmAlloc.c        HmmAlloc, hmmCalloc, hmmReAlloc, hmmSize + free-list helpers
│   ├── HmmFree.c         HmmFree, merge_free_blocks
│   └── heapWrappers.c    malloc / free / calloc / realloc wrappers
└── test/
    └── *.c               your test program(s) - one of them must contain main()
```

> The Makefile expects exactly this layout: sources in `src/`, headers in `inc/`, tests in `test/`.

| File | Contents |
|---|---|
| `Hmm.h` | `struct meta_block`, `heap_lock` declaration, flag masks, `ALIGN`, `MIN_INCREMENT` / `MIN_DECREMENT`, inline helpers (`set_free`, `real_size`, `phys_next`, `phys_prev`, `update_footer`, `is_last`, ...), public prototypes |
| `HmmAlloc.c` | `HmmAlloc`, `hmmCalloc`, `hmmReAlloc`, `hmmSize`, and the helpers `find_block`, `split_block`, `expand_heap`, `insert_in_order`, `remove_from_free_list` |
| `HmmFree.c` | `HmmFree`, `merge_free_blocks` |
| `heapWrappers.c` | The four libc-named wrappers that forward to the Hmm functions |

---

## 4. Memory model

### 4.1 Block layouts

**Allocated block**

```
 block                         block + 8 (returned to the caller, 16-byte aligned)
   │                              │
   ▼                              ▼
   ┌──────────────┬─────────────────────────────────────────────┐
   │ HEADER  8 B  │ PAYLOAD  (block size - 8 bytes)             │
   │ size | PF | F│                                             │
   └──────────────┴─────────────────────────────────────────────┘
   |<------------------- block size (multiple of 16, >= 32) --->|
```

**Free block**

```
   ┌────────┬────────┬────────┬──────────────────────┬─────────┐
   │ HEADER │  next  │  prev  │   (unused space)     │ FOOTER  │
   │ size|F │  8 B   │  8 B   │                      │ size    │
   └────────┴────────┴────────┴──────────────────────┴─────────┘
   |<------ struct meta_block (24 B) ------>|
   minimum free block = 8 + 8 + 8 + 8 = 32 bytes
```

The footer exists only while a block is free. In an allocated block that space belongs to the payload.

### 4.2 Header word

| Bits | Name | Meaning |
|---|---|---|
| 63 ... 2 | size | Block size in bytes. Read with `SIZE_MASK` (`~0x3`). |
| 1 | `PREV_FREE` (`0x2`) | The block physically before this one is free. |
| 0 | `FREE` (`0x1`) | This block is on the free list. |

### 4.3 Finding neighbours

```
phys_next(B) = B + real_size(B)
phys_prev(B) = B - prev_size(B)      /* size read from the previous block's footer,
                                        valid only when PREV_FREE is set on B */
```

### 4.4 Invariants

1. Every block size is a multiple of 16 and at least 32. Block starts are 8 mod 16, so `block + 8` is 16-byte aligned.
2. `FREE` is set exactly for blocks on the free list. `PREV_FREE` mirrors the state of the physical predecessor.
3. A footer is valid only on free blocks.
4. The free list is doubly linked and sorted by ascending size.
5. `HmmFree` merges with free neighbours, so adjacent free blocks do not normally coexist.
6. `is_last(block)` is true when `phys_next(block)` reaches `heap_end`.
7. `HmmAlloc` and `HmmFree` hold `heap_lock` for their whole body.

---

## 5. API reference

### 5.1 Public functions (`Hmm.h`)

| Function | Description | Returns |
|---|---|---|
| `void *HmmAlloc(size_t size)` | Allocates a block with at least `size` usable bytes. | Pointer to the payload, or `NULL` for `size == 0`, on overflow, or when the kernel refuses more memory. |
| `void HmmFree(void *ptr)` | Releases a block. `NULL` and already-free blocks are ignored. | nothing |
| `void *hmmCalloc(size_t nmemb, size_t size)` | Allocates `nmemb * size` bytes and sets them to zero. | Pointer, or `NULL` if the multiplication overflows or allocation fails. |
| `void *hmmReAlloc(void *ptr, size_t reqSize)` | Changes the size of a block. May move it. | New pointer, `ptr` itself if it already fits, or `NULL`. |
| `size_t hmmSize(void *ptr)` | Usable bytes of a block (block size - 8). | Size in bytes, `0` for `NULL`. |

### 5.2 libc wrappers (`heapWrappers.c`)

| libc symbol | Forwards to |
|---|---|
| `malloc(size)` | `HmmAlloc(size)` |
| `free(ptr)` | `HmmFree(ptr)` |
| `calloc(nmemb, size)` | `hmmCalloc(nmemb, size)` |
| `realloc(ptr, size)` | `hmmReAlloc(ptr, size)` |

### 5.3 Edge-case behaviour

| Call | Result |
|---|---|
| `malloc(0)`, `calloc(0, m)`, `calloc(n, 0)` | `NULL` (size 0 is rejected before any work) |
| `calloc(n, m)` with `n * m` overflowing | `NULL` |
| `realloc(NULL, n)` | same as `malloc(n)` |
| `realloc(p, 0)` | frees `p`, returns `NULL` |
| `realloc(p, n)` with `n` <= usable size of `p` | returns `p` unchanged |
| `realloc(p, n)` when allocation fails | `NULL`, the original block stays valid |
| `free(NULL)`, or free of a block already marked free | ignored |

### 5.4 Internal functions

| Function | File | Role |
|---|---|---|
| `find_block(size)` | HmmAlloc.c | First block in the sorted list with `real_size >= size` (best fit) |
| `expand_heap(size)` | HmmAlloc.c | Calls `sbrk()` for `max(size, 128 KiB)`; aligns the break on first use |
| `split_block(block, size)` | HmmAlloc.c | Cuts a block in two and files the remainder |
| `insert_in_order(block)` | HmmAlloc.c | Inserts into the free list, keeping it sorted |
| `remove_from_free_list(block)` | HmmAlloc.c | Unlinks a block (no-op if it is not free) |
| `merge_free_blocks(block)` | HmmFree.c | Forward merge, then backward merge; returns the merged block |

---

## 6. How it works

### 6.1 Allocation: `HmmAlloc(size)`

1. `size == 0`: return `NULL`.
2. `aligned = ALIGN(size + HEADER_SIZE)`. If it is smaller than `size`, the arithmetic overflowed: return `NULL`. Use at least 32 bytes.
3. Lock `heap_lock`.
4. `find_block()` searches the free list for the smallest block that fits. If nothing fits, `expand_heap()` gets at least 128 KiB from the kernel. If that fails: unlock and return `NULL`.
5. `remove_from_free_list()` takes the block out of the list (no effect on a fresh block).
6. If the block is at least `size + 32` bytes, `split_block()` cuts off the remainder and files it.
7. Mark the block allocated; if a block follows, clear its `PREV_FREE` flag.
8. Unlock and return `block + 8`.

### 6.2 Freeing: `HmmFree(ptr)`

1. `NULL`: return. Otherwise lock `heap_lock`; an already-free block: unlock and return.
2. `merge_free_blocks()`: absorb a free next block, then a free previous block.
3. Mark the result free and write its footer. If a block follows, set its `PREV_FREE` flag.
4. If it is the top block and at least 128 KiB, give it back with `sbrk(-size)`. Otherwise insert it into the sorted free list. Unlock and return.

### 6.3 Extras

* **`hmmCalloc`** checks `nmemb * size` for overflow, calls `HmmAlloc`, then zero-fills.
* **`hmmReAlloc`** returns the same pointer when the current block is large enough. Otherwise it allocates a new block, copies the old payload, and frees the old block.
* **`hmmSize`** returns the block size minus the 8-byte header. It reads the header without taking the lock.

The complete decision-by-decision logic is in the [flowchart PDF](#12-flowcharts).

---

## 7. Build manual

### 7.1 Requirements

| Tool | Notes |
|---|---|
| `gcc` | GNU C compiler (the Makefile sets `CC := gcc`) |
| `make` | GNU Make (tested with 4.3) |
| `ar` | from binutils, used for the static library |
| `pthread` | `Hmm.h` includes `<pthread.h>`. Built without extra flags on glibc 2.39; on glibc older than 2.34 add `-pthread` to `CFLAGS` and to your link command |
| Linux | `sbrk()` based |

### 7.2 Directory layout the Makefile expects

```
Makefile
inc/Hmm.h
src/HmmAlloc.c  src/HmmFree.c  src/heapWrappers.c
test/<your test>.c        (needs a main())
```

If your files are all in one folder, create the layout first:

```bash
mkdir -p src inc test
mv HmmAlloc.c HmmFree.c heapWrappers.c src/
mv Hmm.h inc/
```

### 7.3 Make targets

| Command | What it does | Output |
|---|---|---|
| `make` or `make all` | Builds the static library **and** the test program | `build/lib/libhmm.a`, `build/hmm_test` |
| `make static_lib` | Builds the static library only | `build/lib/libhmm.a` |
| `make shared_lib` | Builds the shared library | `build/lib/libhmm.so` |
| `make test` | Builds the test program from `test/*.c` plus the Hmm objects | `build/hmm_test` |
| `make clean` | Deletes the whole `build/` directory | - |

### 7.4 Compiler settings (from the Makefile)

| Variable | Value |
|---|---|
| `CC` | `gcc` |
| `CPPFLAGS` | `-Iinc` |
| `CFLAGS` | `-Wall -Wextra -g -fPIC` |
| `ARFLAGS` | `rcs` |

Dependency files (`-MMD -MP`) are generated, so editing `Hmm.h` rebuilds the objects that include it.

### 7.5 Build output tree

```
build/
├── obj/    HmmAlloc.o  HmmFree.o  heapWrappers.o
├── dep/    HmmAlloc.d  HmmFree.d  heapWrappers.d
├── lib/    libhmm.a  libhmm.so
└── hmm_test
```

### 7.6 Quick start

```bash
make                 # static library + test program
./build/hmm_test     # run your tests
make shared_lib      # optional: also build libhmm.so
make clean           # start over
```

### 7.7 Expected compiler output

Building `HmmAlloc.c` prints one warning:

```
src/HmmAlloc.c:97:21: warning: cast from pointer to integer of different size [-Wpointer-to-int-cast]
```

It comes from `(int)sbrk(0)` in `expand_heap()` and does not stop the build. See [Limitations](#10-limitations-and-notes).

### 7.8 Important: the test program uses Hmm for everything

`make test` links `heapWrappers.o` into `hmm_test`. That replaces `malloc` / `free` for the **whole program**, including the C library's own allocations (for example the buffer behind `printf`). Heap dumps printed by a test will therefore include those allocations. To test `HmmAlloc` / `HmmFree` in isolation, call them directly by name.

---

## 8. Using the library

### 8.1 Call the Hmm functions directly

```c
#include <stdio.h>
#include "Hmm.h"

int main(void)
{
    char *p = HmmAlloc(100);
    printf("usable bytes: %zu\n", hmmSize(p));   /* 104 */
    HmmFree(p);
    return 0;
}
```

### 8.2 Link the static library (replaces `malloc` in your program)

```bash
gcc -Iinc app.c -Lbuild/lib -l:libhmm.a -o app
./app
```

> If both `libhmm.a` and `libhmm.so` are in `build/lib`, plain `-lhmm` picks the **shared** library. Use `-l:libhmm.a` (or the full path `build/lib/libhmm.a`) to force static linking.

### 8.3 Link the shared library

```bash
gcc -Iinc app.c -Lbuild/lib -lhmm -Wl,-rpath,"$PWD/build/lib" -o app
./app
```

Without `-Wl,-rpath,...` set `LD_LIBRARY_PATH=$PWD/build/lib` before running, or the loader will not find `libhmm.so`.

### 8.4 Replace the allocator of an existing program (`LD_PRELOAD`)

```bash
LD_PRELOAD=$PWD/build/lib/libhmm.so ls /usr
LD_PRELOAD=$PWD/build/lib/libhmm.so python3 -c "print(sum(range(10)))"
```

Both commands ran correctly with the current library during testing. Other programs are not guaranteed to work (see the next section).

---

## 9. Worked example

`A = malloc(100)`, `B = malloc(50)`, `C = malloc(200)`, then `free(B)`, `free(A)`, `free(C)`. Output of the real code (calling `HmmAlloc` / `HmmFree` directly):

| Step | Action | Blocks (physical order) | Free list |
|---|---|---|---|
| 1 | `A = malloc(100)` | A 112 · free 130,960 | `[130960]` |
| 2 | `B = malloc(50)` | A 112 · B 64 · free 130,896 | `[130896]` |
| 3 | `C = malloc(200)` | A 112 · B 64 · C 208 · free 130,688 | `[130688]` |
| 4 | `free(B)` | A 112 · **B 64 free** · C 208 · free 130,688 | `[64] ↔ [130688]` |
| 5 | `free(A)` | **A+B 176 free** · C 208 · free 130,688 | `[176] ↔ [130688]` |
| 6 | `free(C)` | one 131,072 block reaches `heap_end` and is returned with `sbrk(-131072)` | empty |

Step 1 shows the heap growing: no free block exists, so `expand_heap()` obtains 128 KiB (131,072 bytes) and splits it. A diagram of each state is in the flowchart PDF (Fig. 7).

---

## 10. Limitations and notes

| Area | Note |
|---|---|
| Thread safety | `HmmAlloc` and `HmmFree` run under one non-recursive mutex (`heap_lock`). `hmmSize` reads a header without the lock (harmless for the size bits). `hmmCalloc` and `hmmReAlloc` are safe because they call the locked functions. A 4-thread random test passed 10 of 10 runs. |
| Mixing allocators | `is_last()` relies on `heap_end`. If libc's own `malloc` or other code also moves the program break (for example when `heapWrappers` is not linked), the assumption fails. A 4-thread test that mixed both allocators crashed; with `heapWrappers` linked it passed. |
| Heap growth boundary | `expand_heap()` has `if (prev_is_free(new_block)) set_prev_free(new_block)`, but it runs right after `new_block->size = inc`, so it is never true. `PREV_FREE` is still not set when the old top block is free, and the two are not merged across that boundary (fragmentation only; a 200,000-operation random test found no corruption). |
| `realloc` | Never shrinks a block and never grows it in place; a larger request always allocates, copies and frees. |
| Pointer checks | `HmmFree` only tests the `FREE` flag. Pointers not obtained from `HmmAlloc`, or blocks already merged into a bigger one, give undefined behaviour. |
| Compiler warning | `(int)sbrk(0)` casts a 64-bit pointer to `int` (`-Wpointer-to-int-cast`). `uintptr_t` would be the clean form. |
| Macros | `MIN_INCREMENT` and `MIN_DECREMENT` expand to unparenthesised `128 * 1024`. `HEADER_SIZE` is used in `HmmAlloc` and `hmmSize`, but `HmmFree` and the pointer step in `hmmSize` still use the literal `8`. |

---

## 11. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `undefined reference to 'main'` when running `make` / `make test` | `test/` has no `.c` file with `main()` | Add a test program to `test/` or build only the library with `make static_lib` |
| `No rule to make target 'src/...'` or no source found | Files are not in `src/` and `inc/` | Create the layout from [7.2](#72-directory-layout-the-makefile-expects) |
| `libhmm.so: cannot open shared object file` | The loader cannot find the shared library | Link with `-Wl,-rpath,"$PWD/build/lib"` or export `LD_LIBRARY_PATH` |
| `-lhmm` links the shared library when you wanted static | `.so` is preferred when both exist | Use `-l:libhmm.a` |
| `missing separator` or odd `^M` characters from `make` | The Makefile has Windows (CRLF) line endings | `sed -i 's/\r$//' Makefile` (or `dos2unix Makefile`) |
| Heap dump in a test shows blocks you did not allocate | `heapWrappers.o` makes libc use Hmm too | Expected; see [7.8](#78-important-the-test-program-uses-hmm-for-everything) |
| `undefined reference to pthread_mutex_lock` (old glibc) | `libpthread` not linked | Add `-pthread` to `CFLAGS` and to your link command |
| Warning `cast from pointer to integer of different size` | `(int)sbrk(0)` in `expand_heap()` | Harmless for the build; see [Limitations](#10-limitations-and-notes) |

The supplied `Makefile` has CRLF line endings. GNU Make 4.3 still built it correctly in testing, but converting it to LF avoids problems with other versions and shells.

---

## 12. Flowcharts

The companion PDF **`Hmm_Flowcharts.pdf`** (14 pages) documents the logic of every function:

| Figure | Page | Content |
|---|---:|---|
| Fig. 1 | 2 | System overview and call graph |
| Fig. 2 | 3 | Block layout, header bits, neighbour navigation, constants |
| Fig. 3 | 4 | `HmmAlloc()` flowchart |
| Fig. 4 | 5 | `HmmFree()` flowchart |
| Fig. 5a-f | 6-10 | `find_block`, `split_block`, `insert_in_order`, `remove_from_free_list`, `expand_heap`, `merge_free_blocks` |
| Fig. 6a-c | 11-12 | libc wrappers, `hmmCalloc`, `hmmSize`, `hmmReAlloc`, edge cases |
| Fig. 7 | 13 | Worked example with heap diagrams |
| Notes | 14 | Invariants and implementation notes |

---

<div align="center">

`Hmm.h` · `HmmAlloc.c` · `HmmFree.c` · `heapWrappers.c` · `Makefile`

</div>
