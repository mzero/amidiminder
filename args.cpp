#include "args.h"

#include "ext/CLI11.hpp"

namespace Args {
  std::string rulesFilePath;
  bool rulesCheckOnly = false;
  bool versionOnly = false;

  bool outputPortDetails = false;

  int exitCode = 0;

  bool parse(int argc, char* argv[]) {
    CLI::App app{"amidiminder - ALSA MIDI connection minder"};

    app.add_flag("-v,--version", versionOnly, "print version and exit");
    app.add_option("-f,--rules-file", rulesFilePath, "file of connection rules");
    app.add_flag("-C,--check-rules", rulesCheckOnly, "check rules file then edit");
    app.add_flag("-p,--port-details", outputPortDetails, "output verbose details of each port");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        exitCode = app.exit(e);
        return false;
    }

    if (versionOnly)
    {
      std::cout << "amidiminder 0.71.0" << std::endl;
      exit(0);
    }

    return true;
  }
}
