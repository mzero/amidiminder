#include "args.h"

#include "ext/CLI11.hpp"

namespace Args {
  Command command = Command::Help; // the default command if none given

  bool outputPortDetails = false;
  std::string rulesFilePath;

  int exitCode = 0;

  bool parse(int argc, char* argv[]) {
    CLI::App app{"Maintain MIDI device connections"};
    app.require_subcommand(0, 1);

    app.add_option("-f,--rules-file", rulesFilePath, "File of connection rules");

    CLI::App *helpApp = app.add_subcommand("help");
    helpApp->description(app.get_help_ptr()->get_description());
    helpApp->parse_complete_callback([](){ command = Command::Help; });

    CLI::App *minderApp = app.add_subcommand("minder", "Run the minder service");
    minderApp->parse_complete_callback([](){ command = Command::Minder; });
    minderApp->add_flag("-p,--port-details", outputPortDetails, "output verbose details of each port");

    CLI::App *checkApp = app.add_subcommand("check", "Check the syntax of a rules file");
    checkApp->parse_complete_callback([](){ command = Command::Check; });
    checkApp->add_option("rules-file", rulesFilePath, "rules file to check")
      ->required();

    try {
        app.parse(argc, argv);
        if (command == Command::Help) {
          app.clear();
          throw CLI::CallForHelp();
        }
    } catch (const CLI::ParseError &e) {
        exitCode = app.exit(e);
        return false;
    }

    return true;
  }
}
