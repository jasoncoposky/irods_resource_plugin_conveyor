#ifndef STANDALONE_MODE
#define IRODS_ENABLE_SYSLOG
#include "irods/irods_logger.hpp"
#else
#include "irods/irods_logger.hpp" // Use mock header
#endif

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <cstdarg>
#include <chrono>

#include <boost/container_hash/hash.hpp>

#include "irods/irods_resource_plugin.hpp"
#include "irods/irods_file_object.hpp"
#include "irods/irods_hierarchy_parser.hpp"
#include "irods/irods_resource_redirect.hpp"
#include "irods/irods_resource_constants.hpp"
#include "irods/rodsErrorTable.h"

#include "libconveyor/conveyor.h"

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>

namespace {
    using log_resc = irods::experimental::log::resource;

#ifdef STANDALONE_MODE
    using rsComm_t = irods::rsComm_t;
#endif

    const std::string CONVEYOR_HANDLE_PROP = "conveyor_handle";
    const std::string CONVEYOR_INTENT_PROP = "conveyor_intent";

    // --- 1. Storage Callbacks (iRODS Hierarchy Aware) ---
    struct conveyor_context {
        irods::resource_ptr child;
        irods::first_class_object_ptr fco;
        rsComm_t* comm;
        std::mutex backend_mutex; 
    };

    ssize_t plugin_pwrite(storage_handle_t h, const void* buf, size_t count, off_t offset) {
        auto* ctx = static_cast<conveyor_context*>(h);
        std::lock_guard<std::mutex> lock(ctx->backend_mutex);
        auto ret = ctx->child->call<const void*, int>(ctx->comm, irods::RESOURCE_OP_WRITE, ctx->fco, buf, (int)count);
        return ret.ok() ? (ssize_t)ret.code() : -1;
    }

    ssize_t plugin_pread(storage_handle_t h, void* buf, size_t count, off_t offset) {
        auto* ctx = static_cast<conveyor_context*>(h);
        std::lock_guard<std::mutex> lock(ctx->backend_mutex);
        auto ret = ctx->child->call<void*, int>(ctx->comm, irods::RESOURCE_OP_READ, ctx->fco, buf, (int)count);
        return ret.ok() ? (ssize_t)ret.code() : -1;
    }

    off_t plugin_lseek(storage_handle_t h, off_t offset, int whence) {
        auto* ctx = static_cast<conveyor_context*>(h);
        std::lock_guard<std::mutex> lock(ctx->backend_mutex);
        auto ret = ctx->child->call<long long, int>(ctx->comm, irods::RESOURCE_OP_LSEEK, ctx->fco, (long long)offset, whence);
        return ret.ok() ? (off_t)ret.code() : -1;
    }

    irods::error check_conveyor_error(conveyor_t* _conv) {
        conveyor_stats_t stats;
        if (conveyor_get_stats(_conv, &stats) == 0 && stats.last_error_code != 0) {
            return ERROR(FILE_WRITE_ERR, 
                "conveyor: background I/O error occurred (errno: " + std::to_string(stats.last_error_code) + ")");
        }
        return SUCCESS();
    }

    irods::error flush_if_needed(irods::plugin_context& _ctx) {
        auto fco = boost::dynamic_pointer_cast<irods::file_object>(_ctx.fco());
        if (!fco) return SUCCESS();
        conveyor_t* conv = nullptr;
        if (fco->get_property<conveyor_t*>(CONVEYOR_HANDLE_PROP, conv).ok()) {
            if (conveyor_flush(conv) == LIBCONVEYOR_ERROR) {
                return check_conveyor_error(conv);
            }
        }
        return SUCCESS();
    }

    // --- 2. Conveyor Mapping ---
    struct conveyor_info {
        conveyor_t* conv;
        conveyor_context* ctx;
    };
    static std::unordered_map<std::string, conveyor_info> g_conveyor_table;
    static std::shared_mutex g_conveyor_mutex;

    irods::error conveyor_get_first_child_resc(irods::plugin_context& _ctx, irods::resource_ptr& _resc) {
        irods::resource_child_map* cmap_ref = nullptr;
        irods::error ret = _ctx.prop_map().get< irods::resource_child_map* >(irods::RESC_CHILD_MAP_PROP, cmap_ref);
        if(!ret.ok() || !cmap_ref) return PASS(ret);
        if (cmap_ref->empty()) return ERROR(SYS_INVALID_INPUT_PARAM, "conveyor: child map is empty");
        _resc = cmap_ref->begin()->second.second;
        return SUCCESS();
    }

irods::error get_conveyor(irods::plugin_context& _ctx, conveyor_t*& conv) {
    auto fco = boost::dynamic_pointer_cast<irods::file_object>(_ctx.fco());
    if (!fco) return ERROR(SYS_INVALID_INPUT_PARAM, "conveyor: null fco");

    if (fco->get_property<conveyor_t*>(CONVEYOR_HANDLE_PROP, conv).ok()) {
        return SUCCESS();
    }

    std::string p_path = fco->physical_path();
    if (p_path.empty()) return ERROR(SYS_INVALID_INPUT_PARAM, "conveyor: empty physical path");

    {
        std::shared_lock lock(g_conveyor_mutex);
        auto it = g_conveyor_table.find(p_path);
        if (it != g_conveyor_table.end()) {
            conv = it->second.conv;
            fco->set_property<conveyor_t*>(CONVEYOR_HANDLE_PROP, conv); 
            return SUCCESS();
        }
    }

    irods::resource_ptr child;
    if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);

        auto* c_ctx = new conveyor_context{child, _ctx.fco(), _ctx.comm()};

        std::string intent = ""; 
        fco->get_property<std::string>(CONVEYOR_INTENT_PROP, intent);

        conveyor_config_t cfg = {0};
        cfg.handle = static_cast<storage_handle_t>(c_ctx);
        cfg.flags = fco->flags();
        cfg.ops = { plugin_pwrite, plugin_pread, plugin_lseek };
        
        cfg.initial_write_size = 64 * 1024 * 1024;
        cfg.initial_read_size = 64 * 1024 * 1024;
        cfg.max_write_size = 2048LL * 1024LL * 1024LL;
        cfg.max_read_size = 2048LL * 1024LL * 1024LL;
        cfg.write_chunk_size = 32 * 1024 * 1024;
        cfg.read_chunk_size = 32 * 1024 * 1024;

        std::string context;
        _ctx.prop_map().get<std::string>(irods::RESOURCE_CONTEXT, context);
        if (!context.empty()) {
            size_t pos = context.find("write_chunk_size=");
            if (pos != std::string::npos) {
                size_t end = context.find(';', pos);
                cfg.write_chunk_size = std::stoull(context.substr(pos + 17, end - (pos + 17)));
            }
            pos = context.find("read_chunk_size=");
            if (pos != std::string::npos) {
                size_t end = context.find(';', pos);
                cfg.read_chunk_size = std::stoull(context.substr(pos + 16, end - (pos + 16)));
            }
        }

        if (intent == "create" || intent == "write") {
            cfg.initial_write_size = std::max(cfg.initial_write_size, cfg.write_chunk_size * 2);
        } else if (intent == "read") {
            cfg.initial_read_size = std::max(cfg.initial_read_size, cfg.read_chunk_size * 2);
        }

        conv = conveyor_create(&cfg);
        if (!conv) { 
            delete c_ctx; 
            return ERROR(SYS_INTERNAL_ERR, "conveyor: failed to create conveyor object"); 
        }

        {
            std::unique_lock lock(g_conveyor_mutex);
            g_conveyor_table[p_path] = {conv, c_ctx};
        }
        
        fco->set_property<conveyor_t*>(CONVEYOR_HANDLE_PROP, conv);

        log_resc::debug("conveyor: created for path [{}] with intent [{}]", p_path, intent);
        return SUCCESS();
    }

    // --- 3. iRODS Operations ---

    irods::error conveyor_file_write(irods::plugin_context& _ctx, const void* _buf, int _len) {
        conveyor_t* conv = nullptr;
        if (auto err = get_conveyor(_ctx, conv); !err.ok()) return PASS(err);

        if (auto err = check_conveyor_error(conv); !err.ok()) return PASS(err);

        ssize_t ret = conveyor_write(conv, _buf, (size_t)_len);
        if (ret == LIBCONVEYOR_ERROR) {
            return ERROR(FILE_WRITE_ERR, "conveyor: write failed");
        }

        auto res = SUCCESS();
        res.code(ret);
        return res;
    }

    irods::error conveyor_file_read(irods::plugin_context& _ctx, void* _buf, int _len) {
        conveyor_t* conv = nullptr;
        if (auto err = get_conveyor(_ctx, conv); !err.ok()) return PASS(err);

        if (auto err = check_conveyor_error(conv); !err.ok()) return PASS(err);

        ssize_t ret = conveyor_read(conv, _buf, (size_t)_len);
        if (ret == LIBCONVEYOR_ERROR) {
            return ERROR(FILE_READ_ERR, "conveyor: read failed");
        }

        auto res = SUCCESS();
        res.code(ret);
        return res;
    }

    irods::error conveyor_file_open(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        
        auto ret = child->call(_ctx.comm(), irods::RESOURCE_OP_OPEN, _ctx.fco());
        if (ret.ok()) {
            conveyor_t* conv = nullptr;
            get_conveyor(_ctx, conv); 
        }
        return ret;
    }

    irods::error conveyor_file_close(irods::plugin_context& _ctx) {
        auto fco = boost::dynamic_pointer_cast<irods::file_object>(_ctx.fco());
        std::string p_path = fco->physical_path();
        
        irods::error final_ret = SUCCESS();
        
        {
            std::unique_lock lock(g_conveyor_mutex);
            auto it = g_conveyor_table.find(p_path);
            if (it != g_conveyor_table.end()) {
                if (conveyor_flush(it->second.conv) == LIBCONVEYOR_ERROR) {
                    final_ret = check_conveyor_error(it->second.conv);
                }
                
                conveyor_destroy(it->second.conv);
                delete it->second.ctx;
                g_conveyor_table.erase(it);
            }
        }

        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        
        auto close_ret = child->call(_ctx.comm(), irods::RESOURCE_OP_CLOSE, _ctx.fco());
        if (!close_ret.ok()) return close_ret;
        
        return final_ret;
    }

    irods::error conveyor_file_create(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        
        auto ret = child->call(_ctx.comm(), irods::RESOURCE_OP_CREATE, _ctx.fco());
        if (ret.ok()) {
            conveyor_t* conv = nullptr;
            get_conveyor(_ctx, conv); 
        }
        return ret;
    }

    irods::error conveyor_file_lseek(irods::plugin_context& _ctx, long long _offset, int _whence) {
        conveyor_t* conv = nullptr;
        if (auto err = get_conveyor(_ctx, conv); !err.ok()) return PASS(err);

        off_t ret = conveyor_lseek(conv, (off_t)_offset, _whence);
        if (ret == LIBCONVEYOR_ERROR) return ERROR(UNIX_FILE_LSEEK_ERR, "conveyor: lseek failed");

        auto res = SUCCESS();
        res.code(ret);
        return res;
    }

    irods::error conveyor_file_unlink(irods::plugin_context& _ctx) {
        if (auto err = flush_if_needed(_ctx); !err.ok()) return PASS(err);
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_UNLINK, _ctx.fco());
    }

    irods::error conveyor_file_stat(irods::plugin_context& _ctx, struct stat* _statbuf) {
        if (auto err = flush_if_needed(_ctx); !err.ok()) return PASS(err);
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call<struct stat*>(_ctx.comm(), irods::RESOURCE_OP_STAT, _ctx.fco(), _statbuf);
    }

    irods::error conveyor_file_mkdir(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_MKDIR, _ctx.fco());
    }

    irods::error conveyor_file_rmdir(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_RMDIR, _ctx.fco());
    }

    irods::error conveyor_file_opendir(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_OPENDIR, _ctx.fco());
    }

    irods::error conveyor_file_closedir(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_CLOSEDIR, _ctx.fco());
    }

    irods::error conveyor_file_readdir(irods::plugin_context& _ctx, struct rodsDirent** _dirent_ptr) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call<struct rodsDirent**>(_ctx.comm(), irods::RESOURCE_OP_READDIR, _ctx.fco(), _dirent_ptr);
    }

    irods::error conveyor_file_rename(irods::plugin_context& _ctx, const char* _new_file_name) {
        if (auto err = flush_if_needed(_ctx); !err.ok()) return PASS(err);
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call<const char*>(_ctx.comm(), irods::RESOURCE_OP_RENAME, _ctx.fco(), _new_file_name);
    }

    irods::error conveyor_file_truncate(irods::plugin_context& _ctx) {
        if (auto err = flush_if_needed(_ctx); !err.ok()) return PASS(err);
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_TRUNCATE, _ctx.fco());
    }

    irods::error conveyor_file_getfsfreespace(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_FREESPACE, _ctx.fco());
    }

    irods::error conveyor_file_registered(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_REGISTERED, _ctx.fco());
    }

    irods::error conveyor_file_unregistered(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_UNREGISTERED, _ctx.fco());
    }

    irods::error conveyor_file_modified(irods::plugin_context& _ctx) {
        irods::resource_ptr child;
        if (auto err = conveyor_get_first_child_resc(_ctx, child); !err.ok()) return PASS(err);
        return child->call(_ctx.comm(), irods::RESOURCE_OP_MODIFIED, _ctx.fco());
    }

    irods::error conveyor_file_notify(irods::plugin_context& _ctx, const std::string* _opr) {
        irods::resource_child_map* cmap_ref = nullptr;
        if(_ctx.prop_map().get< irods::resource_child_map* >(irods::RESC_CHILD_MAP_PROP, cmap_ref).ok() && cmap_ref) {
            for (auto& it : *cmap_ref) it.second.second->call(_ctx.comm(), irods::RESOURCE_OP_NOTIFY, _ctx.fco(), _opr);
        }
        return SUCCESS();
    }

    irods::error conveyor_resolve_hierarchy(
        irods::plugin_context& _ctx,
        const std::string* _op,
        const std::string* _curr_host,
        irods::hierarchy_parser* _parser,
        float* _vote)
    {
        std::string resc_name = irods::get_resource_name(_ctx);
        _parser->add_child(resc_name);
        
        if (_op) {
            auto fco = boost::dynamic_pointer_cast<irods::file_object>(_ctx.fco());
            if (fco) fco->set_property<std::string>(CONVEYOR_INTENT_PROP, *_op);
        }

        irods::resource_ptr child;
        if (!conveyor_get_first_child_resc(_ctx, child).ok()) { *_vote = 0.0f; return SUCCESS(); }
        
        irods::error ret = child->call<const std::string*, const std::string*, irods::hierarchy_parser*, float*>(
            _ctx.comm(), irods::RESOURCE_OP_RESOLVE_RESC_HIER, _ctx.fco(), _op, _curr_host, _parser, _vote);
            
        if (ret.ok() && _op && !(*_op).empty()) {
            *_vote += 0.5f;
        }
        return ret;
    }

    class conveyor_resource : public irods::resource {
    public:
        conveyor_resource(const std::string& _inst, const std::string& _context)
            : irods::resource(_inst, _context) {}
    };

extern "C" {

irods::resource* plugin_factory(const std::string& _inst_name, const std::string& _context) {
    conveyor_resource* resc = new conveyor_resource(_inst_name, _context);
    using std::function;
    using namespace irods;
    resc->add_operation(RESOURCE_OP_CREATE, function<error(plugin_context&)>(conveyor_file_create));
    resc->add_operation(RESOURCE_OP_OPEN,   function<error(plugin_context&)>(conveyor_file_open));
    resc->add_operation(RESOURCE_OP_CLOSE,  function<error(plugin_context&)>(conveyor_file_close));
    resc->add_operation(RESOURCE_OP_READ,   function<error(plugin_context&, void*, int)>(conveyor_file_read));
    resc->add_operation(RESOURCE_OP_WRITE,  function<error(plugin_context&, const void*, int)>(conveyor_file_write));
    resc->add_operation(RESOURCE_OP_LSEEK,  function<error(plugin_context&, long long, int)>(conveyor_file_lseek));
    resc->add_operation(RESOURCE_OP_UNLINK, function<error(plugin_context&)>(conveyor_file_unlink));
    resc->add_operation(RESOURCE_OP_STAT,   function<error(plugin_context&, struct stat*)>(conveyor_file_stat));
    resc->add_operation(RESOURCE_OP_MKDIR,  function<error(plugin_context&)>(conveyor_file_mkdir));
    resc->add_operation(RESOURCE_OP_RMDIR,  function<error(plugin_context&)>(conveyor_file_rmdir));
    resc->add_operation(RESOURCE_OP_OPENDIR, function<error(plugin_context&)>(conveyor_file_opendir));
    resc->add_operation(RESOURCE_OP_CLOSEDIR, function<error(plugin_context&)>(conveyor_file_closedir));
    resc->add_operation(RESOURCE_OP_READDIR, function<error(plugin_context&, struct rodsDirent**)>(conveyor_file_readdir));
    resc->add_operation(RESOURCE_OP_RENAME, function<error(plugin_context&, const char*)>(conveyor_file_rename));
    resc->add_operation(RESOURCE_OP_TRUNCATE, function<error(plugin_context&)>(conveyor_file_truncate));
    resc->add_operation(RESOURCE_OP_FREESPACE, function<error(plugin_context&)>(conveyor_file_getfsfreespace));
    resc->add_operation(RESOURCE_OP_REGISTERED, function<error(plugin_context&)>(conveyor_file_registered));
    resc->add_operation(RESOURCE_OP_UNREGISTERED, function<error(plugin_context&)>(conveyor_file_unregistered));
    resc->add_operation(RESOURCE_OP_MODIFIED, function<error(plugin_context&)>(conveyor_file_modified));
    resc->add_operation(RESOURCE_OP_NOTIFY, function<error(plugin_context&, const std::string*)>(conveyor_file_notify));
    resc->add_operation(RESOURCE_OP_RESOLVE_RESC_HIER, function<error(plugin_context&, const std::string*, const std::string*, hierarchy_parser*, float*)>(conveyor_resolve_hierarchy));
    resc->set_property<std::string>(RESOURCE_CLASS, "coordinating");
    resc->set_property<int>(RESOURCE_CHECK_PATH_PERM, 2);
    resc->set_property<int>(RESOURCE_CREATE_PATH, 1);
    return dynamic_cast<irods::resource*>(resc);
}

} // extern "C"
} // namespace
