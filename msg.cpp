#include "msg.h"

#include <iostream>
#include <ostream>

#include "args.h"

namespace {
  auto cout_iter = std::ostreambuf_iterator<char>(std::cout);
  auto cerr_iter = std::ostreambuf_iterator<char>(std::cerr);
}

namespace Msg {
  void voutput(const char* format, fmt::format_args args) {
    if (Args::output()) {
      fmt::vformat_to(cout_iter, format, args);
      std::cout << std::endl;
    }
  }

  void vdetail(const char* format, fmt::format_args args) {
    if (Args::detail()) {
      fmt::vformat_to(cout_iter, format, args);
      std::cout << std::endl;
    }
  }

  void vdebug(const char* format, fmt::format_args args) {
    if (Args::debug()) {
      fmt::vformat_to(cout_iter, format, args);
      std::cout << std::endl;
    }
  }

  void verror(const char* format, fmt::format_args args) {
      fmt::vformat_to(cerr_iter, format, args);
      std::cerr << std::endl;
  }
}

