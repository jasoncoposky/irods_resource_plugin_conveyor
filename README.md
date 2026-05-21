# iRODS Conveyor Resource Plugin

High-performance coordinating resource plugin for iRODS that implements asynchronous buffering and prefetching using the **libconveyor** compute foundation.

## Overview

The Conveyor resource acts as a high-speed "Implementation Firewall" between iRODS worker threads and the underlying storage system. It offloads slow I/O operations to a high-performance **Citor-backed task pool**, allowing iRODS to maintain high throughput even with high-latency backends (e.g. S3, remote NAS, or slow disks).

### Key Features
- **In-Process Buffering**: Uses `libconveyor` for zero-latency task offloading.
- **Topology-Aware Compute**: Leverages hardware-aligned task dispatching to maximize L3 cache hit rates.
- **Lock-Free Scalability**: Uses concurrent queues for metadata handoff, eliminating mutex contention on the hot path.
- **Adaptive Memory Management**: Automatically scales I/O buffers from 64MB up to 1GB based on workload demand.
- **Asynchronous Write-Behind**: Application writes return instantly once data is safely in the conveyor buffer.
- **Read-Ahead Prefetching**: Proactively fetches data into memory to serve iRODS reads at nanosecond speeds.

## Installation

### Prerequisites
- iRODS Development Headers (4.3.0+)
- CMake 3.15+
- C++20 Compiler (GCC 10+)

### Build & Test (Standalone)
Verify the plugin logic without a full iRODS installation:
```bash
mkdir build_standalone && cd build_standalone
cmake .. -DSTANDALONE_TEST=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
./conveyor_standalone_test
```

## Configuration

The Conveyor resource is a **Coordinating Resource**. It must be placed in a hierarchy above a storage resource.

```bash
iadmin mkresc conveyor_resc coordinating conveyor ""
iadmin addchildtoresc conveyor_resc unixfilesystem_resc
```

## Performance Benefits

By leveraging the new `citor` foundation in `libconveyor`, this plugin provides significant speedups by overlapping I/O with application logic.

### Standalone Benchmark Results
*Simulated Backend Latency: 2000 µs (2ms)*
*Data Volume: 100MB (64KB blocks)*

| Operation | Raw POSIX | Conveyor Plugin | Speedup |
| :--- | :--- | :--- | :--- |
| **Write Throughput** | 28.1 MB/s | **54.9 MB/s** | **~1.95x** |
| **Read Throughput** | 29.1 MB/s | **70.7 MB/s** | **~2.43x** |

- **Sub-microsecond Handoff** between iRODS and the I/O compute pool.
- **Elimination of Context-Switch Storms** during massive parallel file transfers.
- **Adaptive Prefetching** effectively hides 2ms of network latency during reads.

### Run the Benchmark
```bash
cd build_standalone
./conveyor_plugin_benchmark
```

## License
BSD 3-Clause License.
