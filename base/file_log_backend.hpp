#pragma once

#include "filesystem.hpp"
#include "log_backend.hpp"
#include "output_stream.hpp"
#include <chrono>
#include <memory>
#include <string>

namespace jkgm {
    class file_log_backend : public log_backend {
    private:
        std::unique_ptr<output_stream> file;
        // The logger's midend already serializes write_message() with a mutex.
        // Batch lines instead of issuing a synchronous Wine/Win32 WriteFile
        // on every diagnostic, especially in profiling mode.
        std::string pending;
        std::chrono::steady_clock::time_point last_flush;

        void flush_pending();

    public:
        explicit file_log_backend(fs::path const &log_filename);
        ~file_log_backend() override;

        void write_message(std::string_view filename,
                           int line_number,
                           log_level level,
                           std::string_view message) override;
    };
}
