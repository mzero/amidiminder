#include "args-service.h"

#include "ext/CLI11.hpp"
#include "msg.h"

namespace {
  const auto appDescription = "Maintain MIDI device connections";
  const auto appVersion = "0.80";
}

namespace Args {
  Command command = Command::Help; // the default command if none given

  std::string rulesFilePath;

  bool keepObserved = false;
  bool resetHard = false;

  int exitCode = 0;


  bool parse(int argc, char* argv[]) {
    CLI::App app{appDescription};
    app.set_version_flag("--version", appVersion);
    app.require_subcommand(0, 1);

    auto vFlag = app.add_flag("-v,--verbose", [](auto n){ Msg::verbosity = n + 1; }, "Increase level of verbosity");
    auto qFlag = app.add_flag("-q,--quiet",   [](auto  ){ Msg::verbosity = 0; },     "Quiet all normal output");
    qFlag->excludes(vFlag);
    vFlag->option_text(" ");  // no need to spam the help with excludes info
    qFlag->option_text(" ");

    const char* userGroup = "User subcommands";

    CLI::App *checkApp = app.add_subcommand("check", "Check the syntax of a rules file");
    checkApp->group(userGroup);
    checkApp->parse_complete_callback([](){ command = Command::Check; });
    checkApp->add_option("file", rulesFilePath, "Rules file to check; use - for stdin")
      ->option_text("PATH")
      ->required();

    CLI::App *loadApp = app.add_subcommand("load", "Load a new porfile, and reset connections");
    loadApp->group(userGroup);
    loadApp->parse_complete_callback([](){ command = Command::Load; });
    loadApp->add_option("file", rulesFilePath, "Rules file to load; use - for stdin")
      ->option_text("PATH")
      ->required();

    CLI::App *saveApp = app.add_subcommand("save", "Save profile & connections to a rules file");
    saveApp->group(userGroup);
    saveApp->parse_complete_callback([](){ command = Command::Save; });
    saveApp->add_option("file", rulesFilePath, "Rules file save to; use - for stdout")
      ->option_text("PATH")
      ->required();

    CLI::App *resetApp = app.add_subcommand("reset", "Reset connections to match the current profile");
    resetApp->group(userGroup);
    resetApp->parse_complete_callback([](){ command = Command::Reset; });
    resetApp->add_flag("--keep", keepObserved, "Keep the observed rules");
    resetApp->add_flag("--hard", resetHard, "Reload ALSA state while resetting;"
                                          "\nshould never be needed");

    CLI::App *statusApp = app.add_subcommand("status", "Report current status of the daemon");
    statusApp->parse_complete_callback([](){ command = Command::Status; });
    statusApp->group(userGroup);

    CLI::App *helpApp = app.add_subcommand("help");
    helpApp->group(userGroup);
    helpApp->description(app.get_help_ptr()->get_description());
    helpApp->parse_complete_callback([](){ command = Command::Help; });


    const char* systemGroup = "System commands";

    CLI::App *daemonApp = app.add_subcommand("daemon", "Run the minder service");
    daemonApp->group(systemGroup);
    daemonApp->parse_complete_callback([](){ command = Command::Daemon; });


    CLI::App *cltApp = app.add_subcommand("connection-logic-test", "");
    cltApp->group(""); // hide this command
    cltApp->parse_complete_callback([](){ command = Command::ConnectionLogicTest; });

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
