#include "args-user.h"

#include "ext/CLI11.hpp"
#include "msg.h"

namespace {
  const auto appDescription = "Maintain MIDI device connections";
  const auto appVersion = "0.80";
}

namespace Args {
  Command command = Command::View; // the default command if none given

  bool listClients = false;
  bool listPorts = false;
  bool listConnections = false;

  bool listAll = false;
  bool listPlain = false;
  bool listDetails = false;

  bool listNumericSort = false;

  std::string portSender;
  std::string portDest;

  int exitCode = 0;


  bool parse(int argc, char* argv[]) {
    CLI::App app{appDescription};
    app.set_version_flag("--version", appVersion);
    app.require_subcommand(0, 1);

    CLI::App *viewApp = app.add_subcommand("view", "Interactive viewer (default command)");
    viewApp->parse_complete_callback([](){ command = Command::View; });

    CLI::App *listApp = app.add_subcommand("list", "List ports and connections");
    listApp->parse_complete_callback([](){ command = Command::List; });
    listApp->add_flag("--clients",        listClients,      "Output a list of clients");
    listApp->add_flag("-p,--ports",       listPorts,        "Output a list of ports");
    listApp->add_flag("-c,--connections", listConnections,  "Output a list of connections");
    listApp->add_flag("-a,--all",         listAll,          "Include system items");
    listApp->add_flag("-n,--numeric",     listNumericSort,  "Sort items by ALSA number");
    auto plainFlag = listApp->add_flag("--plain", listPlain,
      "Output only the names of the items");
    auto detailFlag = listApp->add_flag("--details", listDetails,
      "Include all ALSA details");
    plainFlag->excludes(detailFlag);
    plainFlag->option_text(" ");  // no need to spam the help with excludes info
    detailFlag->option_text(" ");
    listApp->footer("Ports and connections are listed if no output is explicitly specified.");

    CLI::App *connectApp = app.add_subcommand("connect", "Connect two ports");
    connectApp->parse_complete_callback([](){ command = Command::Connect; });
    connectApp->add_option("sender", portSender, "Sender port");
    connectApp->add_option("dest", portDest, "Destination port");

    CLI::App *disconnectApp = app.add_subcommand("disconnect", "Disconnect two ports");
    disconnectApp->parse_complete_callback([](){ command = Command::Disconnect; });
    disconnectApp->add_option("sender", portSender, "Sender port");
    disconnectApp->add_option("dest", portDest, "Destination port");


    CLI::App *helpApp = app.add_subcommand("help");
    helpApp->description(app.get_help_ptr()->get_description());
    helpApp->parse_complete_callback([](){ command = Command::Help; });

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
