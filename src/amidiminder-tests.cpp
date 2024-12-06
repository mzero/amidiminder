#include "amidiminder.h"

#include "msg.h"


// The adjustment of observed rules when connection and disconnection
// events occur is more complex than it should seem. In the normal cases
// the operations needed are clear and simple. However, there are states
// of the observed and profile rule sets which while they shouldn't occurr
// should none-the-less be dealt with correctly.  This code runs though
// all possible cases.

namespace {
  enum class Expect {
    Empty,
    Connect,
    Disconnect,
  };

  bool checkRules(Expect expect, const ConnectionRules& rules) {
    bool okay = false;
    switch (expect) {
      case Expect::Empty:
        okay = rules.size() == 0;
        break;

      case Expect::Connect:
        okay = rules.size() == 1 && !rules[0].isBlockingRule();
        break;

      case Expect::Disconnect:
        okay = rules.size() == 1 && rules[0].isBlockingRule();
        break;
    }

    if (okay)   Msg::output("PASSED");
    else        Msg::output("FAILED");

    return okay;
  }
}

void MidiMinder::connectionLogicTest() {
  Address portA({ 150, 0 }, true,
    SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_HARDWARE,
    "Controller", "out");

  Address portB({ 200, 0 }, true,
    SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_HARDWARE,
    "Synthesizer", "in");

  portA.primarySender = true;
  portB.primaryDest = true;

  activePorts.clear();
  activePorts[portA.addr] = portA;
  activePorts[portB.addr] = portB;

  ConnectionRules emptyRules;
  ConnectionRules connectRules1;
  ConnectionRules connectRules2;
  ConnectionRules disconnectRules1;
  ConnectionRules disconnectRules2;
  parseRules("Controller --> Synthesizer\n",          connectRules1);
  parseRules("Controller:out --> Synthesizer:in\n",   connectRules2);
  parseRules("Controller -x-> Synthesizer\n",         disconnectRules1);
  parseRules("Controller:out -x-> Synthesizer:in\n",  disconnectRules2);

  snd_seq_connect_t connAtoB = { portA.addr, portB.addr };

  auto dumpRules = [&](const char* source, const ConnectionRules& rules) {
    Msg::output("# {} rules", source);
    if (rules.empty())
      Msg::output("    --- empty ---");
    else
      for (auto& r : rules)
        Msg::output("    {}", r);
  };

  auto dumpBothRules = [&]() {
    dumpRules("profile", profileRules);
    dumpRules("observed", observedRules);
  };

  int failureCount = 0;

  auto testConnection = [&](int n, const char* name,
      const ConnectionRules& pRules, const ConnectionRules& oRules,
      Expect e) {
    Msg::output("--{}-- connect {}", n, name);
    profileRules = pRules;
    observedRules = oRules;
    dumpBothRules();
    activeConnections.clear();
    Msg::output("** simulating connection {}", connAtoB);
    addConnection(connAtoB);
    dumpBothRules();
    if (!checkRules(e, observedRules)) ++failureCount;
    Msg::output("\n\n");
  };

  testConnection(1, "empty/empty",  emptyRules,       emptyRules,       Expect::Connect);
  testConnection(2, "conn/empty",   connectRules1,    emptyRules,       Expect::Empty);
  testConnection(3, "disc/empty",   disconnectRules1, emptyRules,       Expect::Connect);

  testConnection(4, "empty/conn",   emptyRules,       connectRules2,    Expect::Connect);
  testConnection(5, "conn/conn",    connectRules1,    connectRules2,    Expect::Empty);
  testConnection(6, "disc/conn",    disconnectRules1, connectRules2,    Expect::Connect);

  testConnection(7, "empty/disc",   emptyRules,       disconnectRules2, Expect::Connect);
  testConnection(8, "conn/disc",    connectRules1,    disconnectRules2, Expect::Empty);
  testConnection(9, "disc/disc",    disconnectRules1, disconnectRules2, Expect::Connect);

  auto testDiscnnection = [&](int n, const char* name,
      const ConnectionRules& pRules, const ConnectionRules& oRules,
      Expect e) {
    Msg::output("--{}-- disonnect {}", n, name);
    profileRules = pRules;
    observedRules = oRules;
    dumpBothRules();
    activeConnections.clear();
    activeConnections.insert(connAtoB);
    Msg::output("** simulating disconnection {}", connAtoB);
    delConnection(connAtoB);
    dumpBothRules();
    if (!checkRules(e, observedRules)) ++failureCount;
    Msg::output("\n\n");
  };

  testDiscnnection(1, "empty/empty",  emptyRules,       emptyRules,       Expect::Empty);
  testDiscnnection(2, "conn/empty",   connectRules1,    emptyRules,       Expect::Disconnect);
  testDiscnnection(3, "disc/empty",   disconnectRules1, emptyRules,       Expect::Empty);

  testDiscnnection(4, "empty/conn",   emptyRules,       connectRules2,    Expect::Empty);
  testDiscnnection(5, "conn/conn",    connectRules1,    connectRules2,    Expect::Disconnect);
  testDiscnnection(6, "disc/conn",    disconnectRules1, connectRules2,    Expect::Empty);

  testDiscnnection(7, "empty/disc",   emptyRules,       disconnectRules2, Expect::Empty);
  testDiscnnection(8, "conn/disc",    connectRules1,    disconnectRules2, Expect::Disconnect);
  testDiscnnection(9, "disc/disc",    disconnectRules1, disconnectRules2, Expect::Empty);


  observedRules = emptyRules;
  saveObserved();   // clean up what was written

  if (failureCount) {
    Msg::output("*** FAILED ***");
    Msg::output("Total failures: {}", failureCount);
  }
  else {
    Msg::output("*** ALL PASSED ***");
  }
  Msg::output("This concludes the tests. Exiting.");

}
