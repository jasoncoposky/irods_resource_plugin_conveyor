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

// Declaration of the plugin factory (usually provided by iRODS)
extern "C" irods::resource* plugin_factory(const std::string&, const std::string&);

namespace {
    const int SIMULATED_LATENCY_US = 500000; // 500ms latency

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
            add_operation(irods::RESOURCE_OP_READ, std::function<irods::error(irods::plugin_context&, void*, int)>(
                [this](irods::plugin_context& _ctx, void* _buf, int _len) -> irods::error {
                    if (SIMULATED_LATENCY_US > 0) std::this_thread::sleep_for(std::chrono::microseconds(SIMULATED_LATENCY_US));
                    auto fco = boost::dynamic_pointer_cast<irods::file_object>(_ctx.fco());
                    off_t offset = current_offset_.fetch_add(_len);
                    ssize_t ret = pread(fco->file_descriptor(), _buf, _len, offset); 
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
        }
        void reset_offset() { current_offset_ = 0; }
    };
}

int main() {
    std::cout << "Starting libconveyor iRODS plugin performance benchmark..." << std::endl;
    std::cout << "Simulated Backend Latency: " << SIMULATED_LATENCY_US << " us" << std::endl;

    const std::string test_file = "plugin_bench.dat";
    const size_t TOTAL_DATA = 16 * 1024 * 1024; // 16 MB
    const size_t BLOCK_SIZE = 64 * 1024;        // 64 KB blocks
    
    std::filesystem::remove(test_file);
    int fd = open(test_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    assert(fd >= 0);

    std::vector<char> data(32 * 1024 * 1024, 'X');

    // --- RAW BASELINE ---
    std::cout << "\n[Baseline] Running 16MB Raw POSIX Write (Sync)..." << std::endl;
    auto start_bw = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        if (SIMULATED_LATENCY_US > 0) std::this_thread::sleep_for(std::chrono::microseconds(SIMULATED_LATENCY_US));
        ssize_t written = pwrite(fd, data.data(), BLOCK_SIZE, i * BLOCK_SIZE);
        assert(written == (ssize_t)BLOCK_SIZE);
    }
    auto end_bw = std::chrono::high_resolution_clock::now();
    double dur_bw = std::chrono::duration<double>(end_bw - start_bw).count();
    double tp_bw = (TOTAL_DATA / (1024.0 * 1024.0)) / dur_bw;
    std::cout << "Baseline Write Throughput: " << tp_bw << " MB/s" << std::endl;

    // --- RAW MEMCPY BASELINE ---
    std::cout << "\n[Hardware] Running 16MB Raw Memcpy Baseline..." << std::endl;
    std::vector<char> dest_buf(TOTAL_DATA);
    auto start_m = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        std::memcpy(dest_buf.data() + i * BLOCK_SIZE, data.data(), BLOCK_SIZE);
    }
    auto end_m = std::chrono::high_resolution_clock::now();
    double dur_m = std::chrono::duration<double>(end_m - start_m).count();
    std::cout << "Raw Memcpy Time:       " << dur_m << " s" << std::endl;
    std::cout << "Raw Memcpy Throughput: " << (TOTAL_DATA / (1024.0 * 1024.0)) / dur_m << " MB/s" << std::endl;

    // --- TRUE ZERO-COPY BASELINE ---
    std::cout << "\n[Extreme] Running 16MB True Zero-Copy Submission (No Memcpy)..." << std::endl;
    conveyor_config_t zc_cfg = {0};
    zc_cfg.handle = (storage_handle_t)(intptr_t)fd;
    zc_cfg.flags = O_RDWR;
    zc_cfg.ops = { 
        [](storage_handle_t h, const void* b, size_t c, off_t o) { return (ssize_t)c; }, 
        [](storage_handle_t h, void* b, size_t c, off_t o) { return (ssize_t)c; },
        [](storage_handle_t h, off_t o, int w) { return (off_t)o; }
    };
    zc_cfg.initial_write_size = 1024LL * 1024LL * 1024LL;
    zc_cfg.max_write_size = 2048LL * 1024LL * 1024LL;
    zc_cfg.write_chunk_size = 32 * 1024 * 1024;

    conveyor_t* zc_conv = conveyor_create(&zc_cfg);
    assert(zc_conv != nullptr);

    auto start_zc = std::chrono::high_resolution_clock::now();
    // In Zero-Copy mode, 16MB is less than one 32MB segment. 
    // We'll just submit a buffer.
    size_t s = 0;
    void* b = conveyor_get_buffer(zc_conv, &s);
    conveyor_submit_buffer(zc_conv, b, TOTAL_DATA, 0);
    auto end_zc = std::chrono::high_resolution_clock::now();
    
    double dur_zc = std::chrono::duration<double>(end_zc - start_zc).count();
    std::cout << "Zero-Copy Submission Time: " << dur_zc << " s" << std::endl;
    std::cout << "Zero-Copy Throughput:    " << (TOTAL_DATA / (1024.0 * 1024.0)) / dur_zc << " MB/s" << std::endl;
    conveyor_destroy(zc_conv);

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

    // --- WRITE BENCHMARK ---
    std::cout << "\n[Benchmark] Running Async Write through Plugin..." << std::endl;
    auto start_ws = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        auto err = conveyor_resc->call(nullptr, irods::RESOURCE_OP_WRITE, fco, (const void*)data.data(), (int)BLOCK_SIZE);
        if (!err.ok()) {
            std::cerr << "Write failed at block " << i << std::endl;
            return 1;
        }
    }
    auto end_ws = std::chrono::high_resolution_clock::now();
    double dur_ws = std::chrono::duration<double>(end_ws - start_ws).count();
    std::cout << "Submission (Loop) Time: " << dur_ws << " s" << std::endl;
    std::cout << "Submission Throughput: " << (TOTAL_DATA / (1024.0 * 1024.0)) / dur_ws << " MB/s" << std::endl;

    std::cout << "[Benchmark] All writes issued. Closing (flushing)..." << std::endl;
    auto start_wf = std::chrono::high_resolution_clock::now();
    conveyor_resc->call(nullptr, irods::RESOURCE_OP_CLOSE, fco);
    auto end_wf = std::chrono::high_resolution_clock::now();
    
    double dur_wf = std::chrono::duration<double>(end_wf - start_wf).count();
    double dur_w_total = dur_ws + dur_wf;
    double tp_w = (TOTAL_DATA / (1024.0 * 1024.0)) / dur_w_total;
    
    std::cout << "Flush (Close) Time:    " << dur_wf << " s" << std::endl;
    std::cout << "Write Total Time:      " << dur_w_total << " s" << std::endl;
    std::cout << "Overall Throughput:    " << tp_w << " MB/s" << std::endl;

    // --- READ BENCHMARK ---
    child->reset_offset();
    std::vector<char> buffer(BLOCK_SIZE);
    
    std::cout << "\n[Benchmark] Running Prefetched Read through Plugin..." << std::endl;
    auto start_r = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        auto err = conveyor_resc->call(nullptr, irods::RESOURCE_OP_READ, fco, (void*)buffer.data(), (int)BLOCK_SIZE);
        if (!err.ok()) {
            std::cerr << "Read failed at block " << i << std::endl;
            return 1;
        }
    }
    
    auto end_r = std::chrono::high_resolution_clock::now();
    double dur_r = std::chrono::duration<double>(end_r - start_r).count();
    double tp_r = (TOTAL_DATA / (1024.0 * 1024.0)) / dur_r;
    
    std::cout << "Read Total Time: " << dur_r << " s" << std::endl;
    std::cout << "Read Throughput: " << tp_r << " MB/s" << std::endl;

    // --- RAW READ BASELINE ---
    std::cout << "\n[Baseline] Running Raw POSIX Read (Sync)..." << std::endl;
    auto start_br = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        if (SIMULATED_LATENCY_US > 0) std::this_thread::sleep_for(std::chrono::microseconds(SIMULATED_LATENCY_US));
        ssize_t read_bytes = pread(fd, buffer.data(), BLOCK_SIZE, i * BLOCK_SIZE);
        assert(read_bytes == (ssize_t)BLOCK_SIZE);
    }
    auto end_br = std::chrono::high_resolution_clock::now();
    double dur_br = std::chrono::duration<double>(end_br - start_br).count();
    double tp_br = (TOTAL_DATA / (1024.0 * 1024.0)) / dur_br;
    std::cout << "Baseline Read Throughput: " << tp_br << " MB/s" << std::endl;

    // --- FINAL RESULTS ---
    std::cout << "\n--------------------------------------------------" << std::endl;
    std::cout << "FINAL RESULTS (Latency=" << SIMULATED_LATENCY_US << "us)" << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "WRITE SPEEDUP: " << (tp_w / tp_bw) << "x" << std::endl;
    std::cout << "READ SPEEDUP:  " << (tp_r / tp_br) << "x" << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    // cleanup
    close(fd);
    delete conveyor_resc;
    std::filesystem::remove(test_file);
    
    std::cout << "\n[PASS] Plugin benchmark completed." << std::endl;
    return 0;
}
