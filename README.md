# HFT Slab Allocator

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-linux--x86__64-lightgrey)
![Performance](https://img.shields.io/badge/latency-1.65ns-brightgreen)

A deterministic, $O(1)$ memory allocator designed for high-frequency trading (HFT) systems. This project implements a **cache-friendly slab allocator** that eliminates heap fragmentation and minimizes syscall overhead for fixed-size objects (64 bytes).

**Key Metric:** 8.5x faster than standard `malloc` for linear allocation, and **4.5x faster (1.65ns/op)** under high-churn load.

```
.
├── src/
│   ├── slab.c         # Allocator Implementation
│   ├── bench_alloc.c  # Performance Harness
├── include/
│   ├── slab.h         # Public API
│   ├── order.h        # LimitOrder Definition
├── tests/             # Geometry Verification
└── Makefile           # Build System
```

---

## Executive Summary

In latency-critical systems (HFT, Real-Time Embedded), the non-deterministic nature of `malloc` is a liability. General-purpose allocators suffer from:
1.  **Search Overhead:** Traversing free lists to find a "best fit" block.
2.  **Heap Fragmentation:** Memory becoming scattered over time, causing cache misses.
3.  **Syscalls:** Occasional `brk` or `mmap` calls that pause execution.

This Slab Allocator solves these issues by pre-allocating a contiguous block of memory and managing it as a LIFO stack. Pointers to free slots are embedded directly within the free blocks themselves, requiring **zero external metadata** per object.

## Building and Running

### Prerequisites
- GCC compiler supporting C11 standard
- Make build system
- Linux x86_64 platform
- The code is optimized for deterministic latency; the benchmark is the most important part of the build process

### Build the Project
```bash
make
```

### Run the Benchmark
```bash
./bench_alloc
```

### Verification & Memory Safety
This runs the layout verifier, which validates the 64-byte alignment critical for cache-line hits.
```bash
make test
```

### Clean
```bash
make clean
```

---

## Performance Benchmarks

Benchmarks were conducted on **University of Washington's `attu` cluster**, a SuperMicro server node tailored for high-performance computing.

### Hardware Specifications
* **CPU:** 2x Intel Xeon Gold 6132 (14-Core) @ 2.60GHz (Skylake)
* **Memory:** 192GB DDR4 ECC
* **OS:** Linux x86_64 (CentOS/RHEL)

### Aggregate Performance Report
Operation                | Malloc (ns/op) |   Slab (ns/op) |        Speedup
-------------------------|----------------|----------------|----------------
Allocation (Linear)      |          63.68 |           7.30 |          8.72x
Deallocation (Linear)    |          11.49 |           7.39 |          1.56x
Hot Churn (100 batch)    |           7.80 |           1.66 |          4.69x
