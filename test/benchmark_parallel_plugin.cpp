#include "irods/irods_resource_plugin.hpp"
#include "irods/irods_file_object.hpp"
#include "irods/irods_resource_constants.hpp"
#include "libconveyor/conveyor.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <numeric>
#include <atomic>

// Declaration of the plugin factory
extern "C" irods::resource* plugin_factory(const std::string&, const std::string&);

namespace {
    const int SIMULATED_LATENCY_US = 1000; // 1ms latency
    const size_t BLOCK_SIZE = 64 * 1024;   // 64 KB blocks
    const size_t TOTAL_DATA = 1024LL * 1024 * 1024; // 1 GB total
    const int THREAD_COUNTS[] = {1, 2, 4, 8, 16};

    class slow_storage_resource : public irods::resource {
        std::atomic<off_t> current_offset_{0};
    public:
        slow_storage_resource() : irods::resource("slow_storage", "") {
            add_operation(irods::RESOURCE_OP_WRITE, std::function<irods::error(irods::plugin_context&, const void*, int)>(
                [this](irods::plugin_context& _ctx, const void* _buf, int _len) -> irods::error {
                    if (SIMULATED_LATENCY_US > 0) std::this_thread::sleep_for(std::chrono::microseconds(SIMULATED_LATENCY_US));
                    auto fco = boost::dynamic_pointer_cast<irods::file_object>(_ctx.fco());
                    off_t offset = current_offset_.fetch_add(_len);
                    ssize_t ret = pwrite(fco->file_descriptor(), _buf, _len, offset); 
                    auto res = SUCCESS();
                    res.code((int)ret);
                    return res;
                }
            ));
            add_operation(irods::RESOURCE_OP_OPEN, std::function<irods::error(irods::plugin_context&)>(
                [](irods::plugin_context& _ctx) -> irods::error { return SUCCESS(); }
            ));
            add_operation(irods::RESOURCE_OP_CLOSE, std::function<irods::error(irods::plugin_context&)>(
                [](irods::plugin_context& _ctx) -> irods::error { return SUCCESS(); }
            ));
            add_operation(irods::RESOURCE_OP_LSEEK, std::function<irods::error(irods::plugin_context&, long long, int)>(
                [](irods::plugin_context& _ctx, long long _o, int _w) -> irods::error { 
                    auto res = SUCCESS();
                    res.code((int)_o);
                    return res;
                }
            ));
        }
        void reset_offset() { current_offset_ = 0; }
    };
}

void run_parallel_plugin_benchmark(int num_threads) {
    const std::string test_file = "parallel_plugin_" + std::to_string(num_threads) + ".dat";
    int fd = open(test_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    assert(fd >= 0);
    ftruncate(fd, TOTAL_DATA);

    // 1. Setup Plugin & Hierarchy
    irods::resource* conveyor_resc = plugin_factory("conveyor_bench", "");
    auto child = boost::make_shared<slow_storage_resource>();
    irods::resource_child_map cmap;
    cmap["child"] = std::make_pair("child", child);
    conveyor_resc->prop_map().set(irods::RESC_CHILD_MAP_PROP, &cmap);
    conveyor_resc->prop_map().set(irods::RESOURCE_CONTEXT, std::string("write_chunk_size=33554432;read_chunk_size=33554432"));

    auto fco = boost::make_shared<irods::file_object>();
    fco->physical_path(std::filesystem::absolute(test_file).string());
    fco->file_descriptor(fd);
    fco->flags(O_RDWR);

    size_t data_per_thread = TOTAL_DATA / num_threads;
    std::vector<std::thread> threads;
    std::vector<char> block_data(BLOCK_SIZE, 'X');

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (size_t written = 0; written < data_per_thread; written += BLOCK_SIZE) {
                conveyor_resc->call(nullptr, irods::RESOURCE_OP_WRITE, fco, (const void*)block_data.data(), (int)BLOCK_SIZE);
            }
        });
    }

    for (auto& t : threads) t.join();
    
    // Close triggers flush
    conveyor_resc->call(nullptr, irods::RESOURCE_OP_CLOSE, fco);

    auto end = std::chrono::high_resolution_clock::now();
    double duration_s = std::chrono::duration<double>(end - start).count();
    
    std::cout << "Threads: " << num_threads 
              << " | Time: " << duration_s << " s"
              << " | Throughput: " << (TOTAL_DATA / (1024.0 * 1024.0)) / duration_s << " MB/s\n";

    close(fd);
    delete conveyor_resc;
    std::filesystem::remove(test_file);
}

int main() {
    std::cout << "--- iRODS Conveyor Plugin Parallel 1GB Handoff Benchmark ---\n";
    std::cout << "Total Data: 1024 MB\n";
    std::cout << "Backend Latency: " << SIMULATED_LATENCY_US << " us\n\n";

    for (int count : THREAD_COUNTS) {
        run_parallel_plugin_benchmark(count);
    }

    return 0;
}
