#pragma once

#include <map>
#include <set>
#include <string>

#include "ipc.h"
#include "rule.h"
#include "seq.h"

class MidiMinder {
  private:
    Seq seq;
    IPC::Server server;

    ConnectionRules profileRules;
    std::string profileText;

    ConnectionRules observedRules;
    std::string observedText;

    std::map<snd_seq_addr_t, Address> activePorts;
    std::set<snd_seq_connect_t> activeConnections;

    std::set<snd_seq_connect_t> expectedDisconnects;
    std::set<snd_seq_connect_t> expectedConnects;

  public:
    MidiMinder();
    ~MidiMinder();

    void run();

  private:
    void handleSeqEvent(snd_seq_event_t& ev);

    void saveObserved();
    void clearObserved();

    void resetConnectionsHard();
    void resetConnectionsSoft();


    const Address& knownPort(snd_seq_addr_t addr);

    void addPort(const snd_seq_addr_t& addr, bool fromReset = false);
    void delPort(const snd_seq_addr_t& addr);

    void addConnection(const snd_seq_connect_t& conn);
    void delConnection(const snd_seq_connect_t& conn);

  private:
    void handleResetCommand(IPC::Connection& conn, const IPC::Options& opts);
    void handleLoadCommand(IPC::Connection& conn);
    void handleSaveCommand(IPC::Connection& conn);
    void handleStatusCommand(IPC::Connection& conn);

    void handleConnection();

  public:
    static void checkCommand();
    static void sendResetCommand();
    static void sendLoadCommand();
    static void sendSaveCommand();
    static void sendStatusCommand();

  public:
    void connectionLogicTest();

};

