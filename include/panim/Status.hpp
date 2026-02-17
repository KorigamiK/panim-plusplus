#pragma once

#include <string>

namespace panim {

    struct Status {
        bool ok = true;
        std::string message;

        static Status success() { return {true, {}}; }
        static Status failure(std::string msg) { return {false, std::move(msg)}; }
    };

} // namespace panim
