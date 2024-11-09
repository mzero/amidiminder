#include "args.h"

#include "ext/CLI11.hpp"

namespace {
  const auto appDescription = "Maintain MIDI device connections";
  const auto appVersion = "0.70.1";
}

namespace Args {
  Command command = Command::Help; // the default command if none given

  bool outputPortDetails = false;
  std::string rulesFilePath;

  int exitCode = 0;

  bool parse(int argc, char* argv[]) {
    CLI::App app{appDescription};
    app.set_version_flag("-v,--version", appVersion);
    app.require_subcommand(0, 1);

    CLI::App *helpApp = app.add_subcommand("help");
    helpApp->description(app.get_help_ptr()->get_description());
    helpApp->parse_complete_callback([](){ command = Command::Help; });

    CLI::App *daemonApp = app.add_subcommand("daemon", "Run the minder service");
    daemonApp->parse_complete_callback([](){ command = Command::Daemon; });
    daemonApp->add_flag("-p,--port-details", outputPortDetails, "output verbose details of each port");

    CLI::App *checkApp = app.add_subcommand("check", "Check the syntax of a rules file");
    checkApp->parse_complete_callback([](){ command = Command::Check; });
    checkApp->add_option("rules-file", rulesFilePath, "Rules file to check")
      ->required();

    CLI::App *commTestApp = app.add_subcommand("commtest", "Test communication with the minder service");
    commTestApp->parse_complete_callback([](){ command = Command::CommTest; });
    commTestApp->group(""); // hide from help

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
