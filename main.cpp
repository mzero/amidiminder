#include <cstring>
#include <stdexcept>

#include "amidiminder.h"
#include "args.h"
#include "msg.h"


int main(int argc, char *argv[]) {
  if (!Args::parse(argc, argv))
    return Args::exitCode;

  const char* exitPrefix = "";

  try {
    switch (Args::command) {
      case Args::Command::Help:
        // should never happen, handled in Args::parse()
        break;

      case Args::Command::Daemon: {
        exitPrefix = "Fatal: ";
        MidiMinder mm;
        mm.run();
        break;
      }

      case Args::Command::Check:    MidiMinder::checkCommand();         break;
      case Args::Command::Reset:    MidiMinder::sendResetCommand();     break;
      case Args::Command::Load:     MidiMinder::sendLoadCommand();      break;
      case Args::Command::Save:     MidiMinder::sendSaveCommand();      break;
      case Args::Command::Status:   MidiMinder::sendStatusCommand();    break;
    }
  }
  catch (const std::exception& e) {
    if (strlen(e.what()) > 0)
      Msg::error("{}{}", exitPrefix, e.what());
    return 1;
  }

  return 0;
}
