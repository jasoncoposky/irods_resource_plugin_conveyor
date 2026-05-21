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
        void* comm() const { return nullptr; }
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

    class resource {
        std::string name_;
        std::string context_;
        std::map<std::string, boost::any> ops_;
        plugin_property_map props_;
    public:
        resource(const std::string& _n, const std::string& _c) : name_(_n), context_(_c) {}
        virtual ~resource() = default;

        template <typename F>
        void add_operation(const std::string& _op, F _f) {
            ops_[_op] = _f;
        }

        template <typename... Args>
        error call(void* _comm, const std::string& _op, boost::shared_ptr<first_class_object> _fco, Args... _args) {
            auto it = ops_.find(_op);
            if (it == ops_.end()) return ERROR(-1, "op not found");
            plugin_context ctx(props_, _fco);
            auto f = boost::any_cast<std::function<error(plugin_context&, Args...)>>(it->second);
            return f(ctx, _args...);
        }

        // Special case for zero-arg ops
        error call(void* _comm, const std::string& _op, boost::shared_ptr<first_class_object> _fco) {
            auto it = ops_.find(_op);
            if (it == ops_.end()) return ERROR(-1, "op not found");
            plugin_context ctx(props_, _fco);
            auto f = boost::any_cast<std::function<error(plugin_context&)>>(it->second);
            return f(ctx);
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
