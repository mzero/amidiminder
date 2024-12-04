#include "client.h"

#include "msg.h"
#include "seqsnapshot.h"


namespace Client {

  void listCommand() {
    SeqSnapshot s;

    Msg::output("Ports:");
    for (const auto& p : s.ports)
      Msg::output("    {:{cw}} : {:{pw}} [{:3}:{}] {}",
        p.client, p.port, p.addr.client, p.addr.port,
        SeqSnapshot::addressDirStr(p),
        fmt::arg("cw", s.clientWidth), fmt::arg("pw", s.portWidth));

    Msg::output("Connections:");
    for (const auto& c : s.connections) {
      Msg::output("    {} --> {}", c.sender, c.dest);
    }
  }

}
