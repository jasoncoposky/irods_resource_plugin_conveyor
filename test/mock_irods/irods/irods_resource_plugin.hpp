#ifndef MOCK_IRODS_RESOURCE_PLUGIN_HPP
#define MOCK_IRODS_RESOURCE_PLUGIN_HPP

#include "irods_error.hpp"
#include <string>
#include <map>
#include <functional>
#include <boost/any.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

namespace irods {

    // Mock rsComm_t
    struct rsComm_t {};

    class plugin_property_map {
        std::map<std::string, boost::any> props_;
    public:
        template <typename T>
        error get(const std::string& _key, T& _val) const {
            auto it = props_.find(_key);
            if (it == props_.end()) return ERROR(-1, "prop not found");
            _val = boost::any_cast<T>(it->second);
            return SUCCESS();
        }
        template <typename T>
        void set(const std::string& _key, const T& _val) {
            props_[_key] = _val;
        }
    };

    class first_class_object;
    typedef boost::shared_ptr<first_class_object> first_class_object_ptr;
    class resource;
    typedef boost::shared_ptr<resource> resource_ptr;
    typedef std::map<std::string, std::pair<std::string, resource_ptr>> resource_child_map;

    const std::string RESC_CHILD_MAP_PROP = "child_map";
    const std::string RESOURCE_CLASS = "resource_class";
    const std::string RESOURCE_CHECK_PATH_PERM = "check_path_perm";
    const std::string RESOURCE_CREATE_PATH = "create_path";
    const std::string RESOURCE_CONTEXT = "resource_context";

    class plugin_context {
        plugin_property_map& props_;
        boost::shared_ptr<first_class_object> fco_;
    public:
        plugin_context(plugin_property_map& _p, boost::shared_ptr<first_class_object> _f) 
            : props_(_p), fco_(_f) {}
        plugin_property_map& prop_map() const { return props_; }
        boost::shared_ptr<first_class_object> fco() const { return fco_; }
        rsComm_t* comm() const { return nullptr; }
    };

    const std::string RESOURCE_OP_CREATE = "create";
    const std::string RESOURCE_OP_OPEN = "open";
    const std::string RESOURCE_OP_CLOSE = "close";
    const std::string RESOURCE_OP_READ = "read";
    const std::string RESOURCE_OP_WRITE = "write";
    const std::string RESOURCE_OP_LSEEK = "lseek";
    const std::string RESOURCE_OP_UNLINK = "unlink";
    const std::string RESOURCE_OP_STAT = "stat";
    const std::string RESOURCE_OP_MKDIR = "mkdir";
    const std::string RESOURCE_OP_RMDIR = "rmdir";
    const std::string RESOURCE_OP_OPENDIR = "opendir";
    const std::string RESOURCE_OP_CLOSEDIR = "closedir";
    const std::string RESOURCE_OP_READDIR = "readdir";
    const std::string RESOURCE_OP_RENAME = "rename";
    const std::string RESOURCE_OP_TRUNCATE = "truncate";
    const std::string RESOURCE_OP_FREESPACE = "freespace";
    const std::string RESOURCE_OP_REGISTERED = "registered";
    const std::string RESOURCE_OP_UNREGISTERED = "unregistered";
    const std::string RESOURCE_OP_MODIFIED = "modified";
    const std::string RESOURCE_OP_NOTIFY = "notify";
    const std::string RESOURCE_OP_RESOLVE_RESC_HIER = "resolve_hierarchy";

    // Operation Enums for Zero-Overhead Mock Dispatch
    enum class op_idx {
        CREATE = 0,
        OPEN,
        CLOSE,
        READ,
        WRITE,
        LSEEK,
        UNLINK,
        STAT,
        MKDIR,
        RMDIR,
        OPENDIR,
        CLOSEDIR,
        READDIR,
        RENAME,
        TRUNCATE,
        FREESPACE,
        REGISTERED,
        UNREGISTERED,
        MODIFIED,
        NOTIFY,
        RESOLVE_RESC_HIER,
        MAX_OPS
    };

    inline op_idx get_op_idx(const std::string& _op) {
        if (_op == "create") return op_idx::CREATE;
        if (_op == "open") return op_idx::OPEN;
        if (_op == "close") return op_idx::CLOSE;
        if (_op == "read") return op_idx::READ;
        if (_op == "write") return op_idx::WRITE;
        if (_op == "lseek") return op_idx::LSEEK;
        if (_op == "stat") return op_idx::STAT;
        if (_op == "unlink") return op_idx::UNLINK;
        if (_op == "resolve_hierarchy") return op_idx::RESOLVE_RESC_HIER;
        return op_idx::MAX_OPS;
    }

    class resource {
        std::string name_;
        std::string context_;
        
        struct OpBase { virtual ~OpBase() = default; };
        template <typename F>
        struct OpImpl : OpBase {
            F f;
            OpImpl(F _f) : f(_f) {}
        };

        std::unique_ptr<OpBase> ops_table_[(size_t)op_idx::MAX_OPS];
        plugin_property_map props_;
    public:
        resource(const std::string& _n, const std::string& _c) : name_(_n), context_(_c) {}
        virtual ~resource() = default;

        template <typename F>
        void add_operation(const std::string& _op, F _f) {
            auto idx = (size_t)get_op_idx(_op);
            if (idx < (size_t)op_idx::MAX_OPS) {
                ops_table_[idx] = std::make_unique<OpImpl<F>>(_f);
            }
        }

        template <typename... Args>
        error call(void* _comm, const std::string& _op, boost::shared_ptr<first_class_object> _fco, Args... _args) {
            auto idx = (size_t)get_op_idx(_op);
            if (idx >= (size_t)op_idx::MAX_OPS || !ops_table_[idx]) return ERROR(-1, "op not found");
            plugin_context ctx(props_, _fco);
            
            typedef std::function<error(plugin_context&, Args...)> fcn_t;
            auto* impl = static_cast<OpImpl<fcn_t>*>(ops_table_[idx].get());
            return impl->f(ctx, _args...);
        }

        error call(void* _comm, const std::string& _op, boost::shared_ptr<first_class_object> _fco) {
            auto idx = (size_t)get_op_idx(_op);
            if (idx >= (size_t)op_idx::MAX_OPS || !ops_table_[idx]) return ERROR(-1, "op not found");
            plugin_context ctx(props_, _fco);
            
            typedef std::function<error(plugin_context&)> fcn_t;
            auto* impl = static_cast<OpImpl<fcn_t>*>(ops_table_[idx].get());
            return impl->f(ctx);
        }

        template <typename T>
        void set_property(const std::string& _key, const T& _val) {
            props_.set(_key, _val);
        }
        
        plugin_property_map& prop_map() { return props_; }
    };

    inline std::string get_resource_name(plugin_context& _ctx) { return "mock_resc"; }
}

#endif
