#include "irods/irods_resource_plugin.hpp"
#include "irods/irods_file_object.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>

// Declaration of the plugin factory (usually provided by iRODS)
extern "C" irods::resource* plugin_factory(const std::string&, const std::string&);

namespace {
    // Mock child resource that does direct POSIX I/O
    class mock_storage_resource : public irods::resource {
    public:
        mock_storage_resource() : irods::resource("mock_storage", "") {
            add_operation(irods::RESOURCE_OP_WRITE, std::function<irods::error(irods::plugin_context&, const void*, int)>(
                [](irods::plugin_context& _ctx, const void* _buf, int _len) -> irods::error {
                    auto fco = boost::dynamic_pointer_cast<irods::file_object>(_ctx.fco());
                    ssize_t ret = pwrite(fco->file_descriptor(), _buf, _len, 0); 
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
            add_operation(irods::RESOURCE_OP_READ, std::function<irods::error(irods::plugin_context&, void*, int)>(
                [](irods::plugin_context& _ctx, void* _buf, int _len) -> irods::error {
                    auto fco = boost::dynamic_pointer_cast<irods::file_object>(_ctx.fco());
                    ssize_t ret = pread(fco->file_descriptor(), _buf, _len, 0); 
                    auto res = SUCCESS();
                    res.code((int)ret);
                    return res;
                }
            ));
        }
    };
}

int main() {
    std::cout << "Starting libconveyor iRODS plugin standalone test..." << std::endl;

    const std::string test_file = "plugin_standalone_test.dat";
    std::filesystem::remove(test_file);
    int fd = open(test_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    assert(fd >= 0);

    // 1. Create the plugin instance
    irods::resource* conveyor_resc = plugin_factory("conveyor_instance", "");
    assert(conveyor_resc != nullptr);

    // 2. Setup mock child hierarchy
    auto child = boost::make_shared<mock_storage_resource>();
    irods::resource_child_map cmap;
    cmap["child"] = std::make_pair("child", child);
    conveyor_resc->prop_map().set(irods::RESC_CHILD_MAP_PROP, &cmap);

    // 3. Create mock File Object
    auto fco = boost::make_shared<irods::file_object>();
    fco->physical_path(std::filesystem::absolute(test_file).string());
    fco->file_descriptor(fd);
    fco->flags(O_RDWR);

    // 4. Perform Write via Plugin
    std::string data = "High-performance I/O through Citor task foundation!";
    
    std::cout << "[Test] Writing data through plugin..." << std::endl;
    auto err_write = conveyor_resc->call(nullptr, irods::RESOURCE_OP_WRITE, fco, (const void*)data.c_str(), (int)data.size());
    assert(err_write.ok());
    assert(err_write.code() == (int)data.size());

    // 5. Perform Read via Plugin
    std::vector<char> buffer(data.size());
    std::cout << "[Test] Reading data through plugin..." << std::endl;
    auto err_read = conveyor_resc->call(nullptr, irods::RESOURCE_OP_READ, fco, (void*)buffer.data(), (int)buffer.size());
    assert(err_read.ok());
    assert(err_read.code() == (int)data.size());
    assert(std::string(buffer.data(), buffer.size()) == data);

    // 6. Close (triggers conveyor_destroy and flush)
    std::cout << "[Test] Closing conveyor..." << std::endl;
    auto err_close = conveyor_resc->call(nullptr, irods::RESOURCE_OP_CLOSE, fco);
    assert(err_close.ok());

    // 7. Verify file on disk
    close(fd);
    uintmax_t size = std::filesystem::file_size(test_file);
    std::cout << "[Test] Final file size on disk: " << size << " bytes" << std::endl;
    assert(size == data.size());

    std::cout << "[PASS] Standalone plugin test successful!" << std::endl;

    delete conveyor_resc;
    std::filesystem::remove(test_file);
    return 0;
}
