#pragma once

#include <fmt/core.h>
#include <stdexcept>

#include "args.h"


namespace Msg {

  void voutput(const char *format, fmt::format_args args);
  void vdetail(const char *format, fmt::format_args args);
  void vdebug(const char *format, fmt::format_args args);
  void verror(const char *format, fmt::format_args args);

  template <typename... T>
  void output(const char* format, const T&... args) {
    if (Args::output())
      voutput(format, fmt::make_format_args(args...));
  }

  template <typename... T>
  void detail(const char* format, const T&... args) {
    if (Args::detail())
      vdetail(format, fmt::make_format_args(args...));
  }

  template <typename... T>
  void debug(const char* format, const T&... args) {
    if (Args::debug())
      vdebug(format, fmt::make_format_args(args...));
  }

  template <typename... T>
  void error(const char* format, const T&... args) {
    verror(format, fmt::make_format_args(args...));
  }



  std::runtime_error vruntime_error(const char* format, fmt::format_args args);
  std::system_error  vsystem_error(const char* format, fmt::format_args args);

  template <typename... T>
  std::runtime_error
  runtime_error(const char* format, const T&... args)
    { return vruntime_error(format, fmt::make_format_args(args...)); }

  template <typename... T>
  std::system_error
  system_error(const char* format, const T&... args)
    { return vsystem_error(format, fmt::make_format_args(args...)); }

}
