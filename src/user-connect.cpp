#include "user.h"

#include <vector>

#include "args-user.h"
#include "msg.h"
#include "rule.h"
#include "seqsnapshot.h"

namespace User {

  void connectCommand() {
    AddressSpec senderSpec = AddressSpec::parse(Args::portSender, true);
    AddressSpec destSpec = AddressSpec::parse(Args::portDest, true);

    SeqSnapshot snap;
    snap.refresh();

    std::vector<Address> possibleSenders;
    std::vector<Address> possibleDests;
    for (auto& p : snap.ports) {
      if (senderSpec.matchAsSender(p))  possibleSenders.push_back(p);
      if (destSpec.matchAsDest(p))      possibleDests.push_back(p);
    }

    bool proceed = true;

    if (possibleSenders.empty()) {
      proceed = false;
      Msg::error("No ports match the sender: {}", senderSpec);
    }
    else if (possibleSenders.size() > 1 && !senderSpec.isWildcard()) {
      proceed = false;
      Msg::error("The sender {} matches multiple ports, but not a wildcard:", senderSpec);
      for (auto& a : possibleSenders)
        Msg::error("    {}", a);
    }

    if (possibleDests.empty()) {
      proceed = false;
      Msg::error("No ports match the destination: {}", destSpec);
    }
    else if (possibleDests.size() > 1 && !destSpec.isWildcard()) {
      proceed = false;
      Msg::error("The destination {} matches multiple ports, but not a wildcard:", destSpec);
      for (auto& a : possibleDests)
        Msg::error("    {}", a);
    }

    if (!proceed)
      throw Msg::runtime_error("No connections made");

    for (auto& sender : possibleSenders) {
      for (auto& dest : possibleDests) {
        snap.seq.connect(sender.addr, dest.addr);
        Msg::output("Connected {} --> {}", sender, dest);
      }
    }
  }

  void disconnectCommand() {
    AddressSpec senderSpec = AddressSpec::parse(Args::portSender, true);
    AddressSpec destSpec = AddressSpec::parse(Args::portDest, true);
    bool wildcarded = senderSpec.isWildcard() || destSpec.isWildcard();

    SeqSnapshot snap;
    snap.refresh();

    std::vector<SeqSnapshot::Connection> candidates;
    for (auto& c : snap.connections) {
      if (senderSpec.matchAsSender(c.sender)
      && (destSpec.matchAsDest(c.dest)))
        candidates.push_back(c);
    }

    if (candidates.empty())
      throw Msg::runtime_error("No connections match those ports");

    if (!wildcarded && candidates.size() > 1) {
      Msg::error("Matched {} connections, but no wildcards in specification:",
        candidates.size());
      for (auto& c : candidates) {
        Msg::error("    {} -> {}", c.sender, c.dest);
      }
      throw Msg::runtime_error("Did not disconnect any");
    }

    for (auto& conn : candidates) {
      snap.seq.disconnect({conn.sender.addr, conn.dest.addr});
      Msg::output("Disonnected {} -x-> {}", conn.sender, conn.dest);
    }
  }

}
