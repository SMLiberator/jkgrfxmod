#include "file_log_backend.hpp"
#include "format.hpp"
#include "pipe.hpp"

namespace {
    // Bound memory use and the maximum latency of a log line during active
    // logging. The buffer is also written on normal backend destruction.
    constexpr size_t log_batch_bytes = 64U * 1024U;
    constexpr auto log_batch_interval = std::chrono::milliseconds(500);
}

jkgm::file_log_backend::file_log_backend(fs::path const &log_filename)
    : file(std::make_unique<fd_output_stream>(make_output_file_append_pipe(log_filename)))
    , last_flush(std::chrono::steady_clock::now())
{
    pending.reserve(log_batch_bytes);
}

jkgm::file_log_backend::~file_log_backend()
{
    flush_pending();
}

void jkgm::file_log_backend::flush_pending()
{
    if(!pending.empty()) {
        file->write(make_span(pending));
        pending.clear();
    }
    last_flush = std::chrono::steady_clock::now();
}

void jkgm::file_log_backend::write_message(std::string_view filename,
                                           int line_number,
                                           log_level level,
                                           std::string_view message)
{
    auto const line =
        str(fmt(filename, "|", line_number, "|", to_string(level), "> ", message, "\n"));
    if(pending.size() + line.size() > log_batch_bytes) {
        flush_pending();
    }
    pending.append(line);

    // Preserve crash-relevant errors and warnings on disk immediately. Info
    // and debug lines may be delayed by up to one interval while logs flow;
    // they are always flushed when the backend is destroyed normally.
    if(level == log_level::error || level == log_level::warning ||
       pending.size() >= log_batch_bytes ||
       std::chrono::steady_clock::now() - last_flush >= log_batch_interval) {
        flush_pending();
    }
}
