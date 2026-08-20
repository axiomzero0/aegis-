// common/Diagnostics.cpp
#include "aegis/support/Diagnostics.hpp"

#include <cstdarg>

namespace aegis {

void DiagnosticSink::report(Error e) noexcept {
    if (e.severity >= Severity::Error) {
        ++error_count_;
    }
    // Compact textual encoding: "<sev>:<cat>:<msg_id>:<line>:<col>\n"
    // Hot path here is intentionally minimal — we buffer and flush later.
    char line[64];
    int n = std::snprintf(line, sizeof(line),
                          "%u:%u:%u:%u:%u\n",
                          static_cast<unsigned>(e.severity),
                          static_cast<unsigned>(e.category),
                          e.message_id,
                          e.span.line,
                          e.span.col);
    if (n > 0) {
        const size_t len = static_cast<size_t>(n);
        buffer_.insert(buffer_.end(), line, line + len);
        if (out_) {
            std::fwrite(line, 1, len, out_);
        }
    }
}

void DiagnosticSink::note(std::string_view text) noexcept {
    buffer_.insert(buffer_.end(), text.begin(), text.end());
    buffer_.push_back('\n');
    if (out_) {
        std::fwrite(text.data(), 1, text.size(), out_);
        std::fputc('\n', out_);
    }
}

} // namespace aegis
