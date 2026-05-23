#ifndef MOCK_IRODS_LOGGER_HPP
#define MOCK_IRODS_LOGGER_HPP

#include <iostream>
#include <string>
#include <fmt/format.h>

namespace irods {
    namespace experimental {
        namespace log {
            struct resource {
                template<typename... Args>
                static void debug(const std::string& fmt, Args&&... args) {
                    // std::cout << "[DEBUG] " << fmt::format(fmt, std::forward<Args>(args)...) << std::endl;
                }
                template<typename... Args>
                static void error(const std::string& fmt, Args&&... args) {
                    std::cerr << "[ERROR] " << fmt::format(fmt, std::forward<Args>(args)...) << std::endl;
                }
            };
        }
    }
}

#endif
