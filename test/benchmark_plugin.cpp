#include "irods/irods_resource_plugin.hpp"
#include "irods/irods_file_object.hpp"
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
    const int SIMULATED_LATENCY_US = 2000; // 2ms latency

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
    const size_t TOTAL_DATA = 100 * 1024 * 1024; // 100 MB
    const size_t BLOCK_SIZE = 64 * 1024;        // 64 KB blocks
    
    std::filesystem::remove(test_file);
    int fd = open(test_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    assert(fd >= 0);

    std::vector<char> data(BLOCK_SIZE, 'X');

    // --- RAW BASELINE ---
    std::cout << "\n[Baseline] Running 100MB Raw POSIX Write (Sync)..." << std::endl;
    auto start_bw = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        if (SIMULATED_LATENCY_US > 0) std::this_thread::sleep_for(std::chrono::microseconds(SIMULATED_LATENCY_US));
        ssize_t written = pwrite(fd, data.data(), data.size(), i * BLOCK_SIZE);
        assert(written == (ssize_t)BLOCK_SIZE);
    }
    auto end_bw = std::chrono::high_resolution_clock::now();
    double dur_bw = std::chrono::duration<double>(end_bw - start_bw).count();
    double tp_bw = (TOTAL_DATA / (1024.0 * 1024.0)) / dur_bw;
    std::cout << "Baseline Write Throughput: " << tp_bw << " MB/s" << std::endl;

    // 1. Setup Plugin & Hierarchy
    irods::resource* conveyor_resc = plugin_factory("conveyor_bench", "");
    auto child = boost::make_shared<slow_storage_resource>();
    irods::resource_child_map cmap;
    cmap["child"] = std::make_pair("child", child);
    conveyor_resc->prop_map().set(irods::RESC_CHILD_MAP_PROP, &cmap);

    auto fco = boost::make_shared<irods::file_object>();
    fco->physical_path(std::filesystem::absolute(test_file).string());
    fco->file_descriptor(fd);
    fco->flags(O_RDWR);

    // --- WRITE BENCHMARK ---
    std::cout << "\n[Benchmark] Running 100MB Async Write through Plugin..." << std::endl;
    auto start_w = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        auto err = conveyor_resc->call(nullptr, irods::RESOURCE_OP_WRITE, fco, (const void*)data.data(), (int)data.size());
        if (!err.ok()) {
            std::cerr << "Write failed at block " << i << std::endl;
            return 1;
        }
    }
    
    std::cout << "[Benchmark] All writes issued. Closing (flushing)..." << std::endl;
    conveyor_resc->call(nullptr, irods::RESOURCE_OP_CLOSE, fco);
    
    auto end_w = std::chrono::high_resolution_clock::now();
    double dur_w = std::chrono::duration<double>(end_w - start_w).count();
    double tp_w = (TOTAL_DATA / (1024.0 * 1024.0)) / dur_w;
    
    std::cout << "Write Total Time: " << dur_w << " s" << std::endl;
    std::cout << "Write Throughput: " << tp_w << " MB/s" << std::endl;

    // --- READ BENCHMARK ---
    child->reset_offset();
    std::vector<char> buffer(BLOCK_SIZE);
    
    std::cout << "\n[Benchmark] Running 100MB Prefetched Read through Plugin..." << std::endl;
    auto start_r = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        auto err = conveyor_resc->call(nullptr, irods::RESOURCE_OP_READ, fco, (void*)buffer.data(), (int)buffer.size());
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
    std::cout << "\n[Baseline] Running 100MB Raw POSIX Read (Sync)..." << std::endl;
    auto start_br = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < TOTAL_DATA / BLOCK_SIZE; ++i) {
        if (SIMULATED_LATENCY_US > 0) std::this_thread::sleep_for(std::chrono::microseconds(SIMULATED_LATENCY_US));
        ssize_t read_bytes = pread(fd, buffer.data(), buffer.size(), i * BLOCK_SIZE);
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
