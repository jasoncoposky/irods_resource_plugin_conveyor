# iRODS Conveyor Resource Plugin

High-performance coordinating resource plugin for iRODS that implements asynchronous buffering and prefetching using the **libconveyor** compute foundation.

## Overview

The Conveyor resource acts as a high-speed "Implementation Firewall" between iRODS worker threads and the underlying storage system. It offloads slow I/O operations to a high-performance **Citor-backed task pool**, allowing iRODS to maintain spectacular throughput even with high-latency backends (e.g. S3, remote NAS, or high-latency disk arrays).

### Key Features
- **Extreme Write Acceleration**: Achieves massive throughput on high-latency backends by offloading writes to a lock-free, asynchronous segment pool.
- **Enterprise Scalability**: Utilizes a shared, reference-counted **ThreadPool** architecture. A single compute engine serves all open file streams, allowing the plugin to scale to thousands of concurrent operations without resource exhaustion.
- **Robust Error Propagation**: Background I/O errors are proactively surfaced back to the iRODS error stack during writes, reads, and closures.
- **Strict Metadata Consistency**: Mandatory flushes ensure the underlying storage is perfectly synchronized before iRODS performs catalog-altering operations (`stat`, `unlink`, `rename`, `truncate`).
- **Zero-Contention Hot-Path**: Leverages parallel "Reserve-and-Copy" logic to perform memory copies outside of global locks, returning control to iRODS agents instantly.
- **Adaptive Buffer Management**: Starts with conservative 64MB initial buffers and grows on-demand up to 2GB, eliminating the "initialization tax" for small-to-medium files.

## Installation

### Prerequisites
- iRODS Development Headers (4.3.0+)
- CMake 3.15+
- C++20 Compiler (GCC 10+)

### Build & Test (Standalone)
Verify the plugin logic and benchmark performance without a full iRODS installation:
```bash
mkdir build && cd build
cmake .. -DSTANDALONE_TEST=ON -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./conveyor_plugin_benchmark
```

## Configuration

The Conveyor resource is a **Coordinating Resource**. It must be placed in a hierarchy above a storage resource.

```bash
iadmin mkresc conveyor_resc coordinating conveyor ""
iadmin addchildtoresc conveyor_resc unixfilesystem_resc
```

### Performance Tuning
Tune the engine for high-latency links via the resource context:
```bash
iadmin modresc conveyor_resc context "write_chunk_size=33554432;read_chunk_size=33554432"
```

## Performance Results (Verified)

Benchmark conditions: **50 ms** Simulated Backend Latency, **16 MB** Data Volume.

| Operation | Raw POSIX (Sync) | iRODS Plugin (Async) | **Speedup** |
| :--- | :--- | :--- | :--- |
| **Write Total Time** | 12.8 s | **0.65 s** | **~20x** |
| **Parallel Handoff (16 Threads)** | - | **13,457 MB/s** | **NEW** |

- **Zero-Latency Write Handoff**: Concurrent agent throughput exceeds 13 GB/s, saturating local memory-bus speeds.
- **Enterprise Stability**: Transitioned to a persistent **ThreadPool Singleton** to eliminate affinity races and ensure stable teardown in high-concurrency environments.
- **Consolidated Compute**: Shared background threads amortize initialization costs and minimize system context-switching.

## License
BSD 3-Clause License.
