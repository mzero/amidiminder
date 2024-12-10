#include "user.h"

#include "fmt/format.h"

#include "args-user.h"
#include "msg.h"
#include "seqsnapshot.h"


namespace User {

  void listCommand() {
    SeqSnapshot s;
    s.includeAllItems = Args::listAll;
    s.numericSort = Args::listNumericSort;
    s.useLongPortNames = Args::listLongPortNames;
    s.refresh();

    if (!Args::listClients && !Args::listPorts && !Args::listConnections)
      Args::listPorts = Args::listConnections = true;

    if (Args::listClients) {
      if (Args::listPlain) {
        for (const auto& c : s.clients)
          std::cout << c.name << '\n';
      }
      else {
        std::cout << "Clients:\n";
        for (const auto& c : s.clients) {
          fmt::print("    {:{cw}} [{:3}]",
            c.name, c.id,
            fmt::arg("cw", s.clientWidth));
          if (Args::listDetails)
            std::cout << " : " << c.details;
          std::cout << '\n';
        }
        if (s.clients.empty())
          std::cout << "    -- no clients --\n";
      }
    }

    if (Args::listPorts) {
      if (Args::listPlain) {
        for (const auto& p : s.ports)
          fmt::print("{}:{}\n", p.client, p.port);
      }
      else {
        std::cout << "Ports:\n";
        for (const auto& p : s.ports) {
          fmt::print("    {:{cw}} : {:{pw}} [{:3}:{}] {}\n",
            p.client, p.port, p.addr.client, p.addr.port,
            SeqSnapshot::addressDirStr(p),
            fmt::arg("cw", s.clientWidth), fmt::arg("pw", s.portWidth));
          if (Args::listDetails) {
            fmt::print("        {}\n", p.typeString());
            fmt::print("        {}\n\n", p.capsString());
          }
        }
        if (s.ports.empty())
          std::cout << "    -- no ports --\n";
      }
    }

    if (Args::listConnections) {
      if (Args::listPlain) {
        for (const auto& c : s.connections)
          fmt::print("{}:{} --> {}:{}\n",
            c.sender.client, c.sender.port,
            c.dest.client, c.dest.port);
      }
      else {
        std::cout << "Connections:\n";
        for (const auto& c : s.connections) {
          fmt::print("    {} --> {}\n", c.sender, c.dest);
        }
        if (s.connections.empty())
          std::cout << "    -- no connections --\n";
      }
    }

    std::cout.flush();
  }

}
