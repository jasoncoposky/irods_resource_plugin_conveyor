#ifndef MOCK_IRODS_ERROR_HPP
#define MOCK_IRODS_ERROR_HPP

#include <string>

namespace irods {
    class error {
        bool ok_;
        int code_;
    public:
        error() : ok_(true), code_(0) {}
        error(bool _ok, int _code, const std::string& _msg) : ok_(_ok), code_(_code) {}
        bool ok() const { return ok_; }
        int code() const { return code_; }
        void code(int _c) { code_ = _c; }

        // Comparison for std::function matching
        bool operator==(const error& _rhs) const { return ok_ == _rhs.ok_ && code_ == _rhs.code_; }
    };

    inline error SUCCESS() { return error(true, 0, ""); }
    inline error PASS(const error& _e) { return _e; }
    inline error ERROR(int _code, const std::string& _msg) { return error(false, _code, _msg); }
}

#define SUCCESS() irods::SUCCESS()
#define PASS(e) irods::PASS(e)
#define ERROR(c, m) irods::ERROR(c, m)

#endif
