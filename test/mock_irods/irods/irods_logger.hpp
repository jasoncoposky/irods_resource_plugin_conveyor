#ifndef MOCK_IRODS_LOGGER_HPP
#define MOCK_IRODS_LOGGER_HPP

#include <iostream>

namespace irods {
    namespace experimental {
        namespace log {
            struct resource {
                static constexpr int debug = 0;
                static constexpr int error = 1;
            };
        }
    }
}

#endif
