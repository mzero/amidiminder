#include "msg.h"

#include <fmt/format.h>


namespace {
  void vprint_msg(FILE* f, const char* format, fmt::format_args args) {
    fmt::vprint(f, format, args);
    fputc('\n', f);
    fflush(f);
  }
}
namespace Msg {

  int verbosity = 1;

  void voutput(const char* format, fmt::format_args args) {
    if (output()) vprint_msg(stdout, format, args);
  }

  void vdetail(const char* format, fmt::format_args args) {
    if (detail()) vprint_msg(stdout, format, args);
  }

  void vdebug(const char* format, fmt::format_args args) {
    if (debug()) vprint_msg(stdout, format, args);
  }

  void verror(const char* format, fmt::format_args args) {
    vprint_msg(stderr, format, args);
  }


  std::runtime_error vruntime_error(const char* format, fmt::format_args args) {
    return std::runtime_error(fmt::vformat(format, args));
  }

  std::system_error vsystem_error(const char* format, fmt::format_args args) {
    auto ec = std::error_code(errno, std::generic_category());
    return std::system_error(ec, fmt::vformat(format, args));
  }

}

