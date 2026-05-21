#ifndef MOCK_IRODS_FILE_OBJECT_HPP
#define MOCK_IRODS_FILE_OBJECT_HPP

#include <string>
#include <map>
#include <boost/any.hpp>
#include <boost/shared_ptr.hpp>

namespace irods {
    class first_class_object {
    public:
        virtual ~first_class_object() = default;
    };

    class file_object : public first_class_object {
        std::string physical_path_;
        int fd_;
        int flags_;
        std::map<std::string, boost::any> properties_;
    public:
        file_object() : fd_(-1), flags_(0) {}
        void physical_path(const std::string& _p) { physical_path_ = _p; }
        std::string physical_path() const { return physical_path_; }
        void file_descriptor(int _fd) { fd_ = _fd; }
        int file_descriptor() const { return fd_; }
        void flags(int _f) { flags_ = _f; }
        int flags() const { return flags_; }

        template <typename T>
        error get_property(const std::string& _key, T& _val) const {
            auto it = properties_.find(_key);
            if (it == properties_.end()) return ERROR(-1, "property not found");
            _val = boost::any_cast<T>(it->second);
            return SUCCESS();
        }

        template <typename T>
        void set_property(const std::string& _key, const T& _val) {
            properties_[_key] = _val;
        }

        void remove_property(const std::string& _key) {
            properties_.erase(_key);
        }
    };
}

#endif
