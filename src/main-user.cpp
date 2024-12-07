#include <cstring>
#include <stdexcept>

#include "args-user.h"
#include "msg.h"
#include "service.h"
#include "user.h"


int main(int argc, char *argv[]) {
  if (!Args::parse(argc, argv))
    return Args::exitCode;

  try {
    switch (Args::command) {
      case Args::Command::Help:
        // should never happen, handled in Args::parse()
        break;

      case Args::Command::List:     Client::listCommand();              break;
      case Args::Command::View:     Client::viewCommand();              break;
      case Args::Command::Connect:     Client::connectCommand();        break;
      case Args::Command::Disconnect:  Client::disconnectCommand();     break;
    }
  }
  catch (const std::exception& e) {
    if (strlen(e.what()) > 0)
      Msg::error("{}", e.what());
    return 1;
  }

  return 0;
}
