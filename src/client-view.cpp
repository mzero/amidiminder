#include "client.h"

#include <fmt/format.h>

#include "seqsnapshot.h"
#include "term.h"


namespace {
  template<typename T>
  void forwardSelection(
      std::size_t& selection,
      const std::vector<T>& items,
      bool (T::*filter)() const)
  {
      size_t i = selection;
      while (true) {
        i += 1;
        if (i >= items.size()) return;
        if ((items[i].*filter)()) {
          selection = i;
          return;
        }
      }
  }

  template<typename T>
  void backwardSelection(
      std::size_t& selection,
      const std::vector<T>& items,
      bool (T::*filter)() const)
  {
      size_t i = selection;
      while (true) {
        if (i == 0) return;
        i -= 1;
        if ((items[i].*filter)()) {
          selection = i;
          return;
        }
      }
  }

  constexpr bool utf8 = true;
  constexpr const char* boxVertical     = utf8 ? "\xe2\x94\x82" : "|"; // U+2502
  constexpr const char* boxHorizontal   = utf8 ? "\xe2\x94\x80" : "-"; // U+2500
  constexpr const char* boxCornerTL     = utf8 ? "\xe2\x94\x8c" : "+"; // U+250c
  constexpr const char* boxCornerTR     = utf8 ? "\xe2\x94\x90" : "+"; // U+2510
  constexpr const char* boxCornerBL     = utf8 ? "\xe2\x94\x94" : "+"; // U+2514
  constexpr const char* boxCornerBR     = utf8 ? "\xe2\x94\x98" : "+"; // U+2518

  enum class Mode {
    menu,

    pickSender,
    pickDest,
    confirmConnection,

    pickConnection,
    confirmDisconnection,

    quit,
  };

  struct View {
    Term& term;
    SeqSnapshot seqState;

    View(Term& t) : term(t) { }

    std::size_t portsRow;
    std::size_t connectionsRow;
    std::size_t promptRow;

    void layout();

    Mode mode;

    std::size_t selectedSender;
    std::size_t selectedDest;
    std::size_t selectedConnection;

    std::string message;
    void setMessage(const std::string&);

    bool portsDirty = false;
    bool connectionsDirty = false;
    bool promptDirty = false;

    void drawPorts();
    void drawConnections();
    void drawPrompt();

    void render();

    void handleEvent(const Term::Event& ev);
    bool handleGlobalEvent(const Term::Event& ev);
    bool handleMenuEvent(const Term::Event& ev);
    bool handlePortPickerEvent(const Term::Event& ev);
    bool handleConnectionPickerEvent(const Term::Event& ev);
    bool handleConfirmEvent(const Term::Event& ev);

    void backup(bool);

    void run();

    void debugMessage(const std::string&);

  };


  void View::layout() {
    auto portsH = 2 + seqState.ports.size() + 1;     // incl. border rows
    auto connectionsH = 2 + seqState.connections.size() + 1;
    auto promptH = 2; // prompt and message

    promptRow = term.rows() + 1 - promptH;
    connectionsRow = promptRow - connectionsH;
    portsRow = connectionsRow - portsH;

    portsDirty = true;
    connectionsDirty = true;
    promptDirty = true;

    term.clearDisplay();
  }

  void View::render() {

    if (portsDirty)       drawPorts();
    if (connectionsDirty) drawConnections();
    if (promptDirty)      drawPrompt();

    std::cout.flush();
    portsDirty = connectionsDirty = promptDirty = false;
  }

  void View::drawPorts() {
    bool picking = false;
    bool confirming = false;
    size_t s1 = seqState.numPorts;
    size_t s2 = seqState.numPorts;
    size_t inv = seqState.numPorts;
    auto func = &Address::canBeSender;

    switch (mode) {
      case Mode::pickSender:
        picking = true;
        s1 = selectedSender;
        inv = selectedSender;
        break;
      case Mode::pickDest:
        picking = true;
        s1 = selectedSender;
        s2 = selectedDest;
        inv = selectedDest;
        func = &Address::canBeDest;
        break;
      case Mode::confirmConnection:
        confirming = true;
        s1 = selectedSender;
        s2 = selectedDest;
        break;
      default:
        break;
    }
    int y = portsRow;

    term.clearLine(y++);
    fmt::print("{}{} Ports ", boxCornerTL, boxHorizontal);
    for (auto i = seqState.clientWidth + seqState.portWidth + 15; i; --i)
      std::cout << boxHorizontal;
    term.clearLine(y++);
    std::cout << boxVertical;

    char label = 'a';
    size_t index = 0;
    for (auto& p : seqState.ports) {
      term.clearLine(y);
      std::cout << boxVertical << ' ';

      bool isS1 = index == s1;
      bool isS2 = index == s2;
      if (index == inv)                     std::cout << Term::Style::inverse;
      else if (isS1 || isS2)                std::cout << Term::Style::bold;
      else if (confirming)                  std::cout << Term::Style::dim;
      else if (picking && !(p.*func)())     std::cout << Term::Style::dim;

      if (picking) std::cout << label << ')';
      else         std::cout << "  ";

      fmt::print(" {:{cw}} : {:{pw}} [{:3}:{}] {}{}",
        p.client, p.port, p.addr.client, p.addr.port,
        (isS1 | isS2) ? "    " : "",
        (isS1 | isS2) ? SeqSnapshot::dirStr(isS1, isS2)
                      : SeqSnapshot::addressDirStr(p),
        fmt::arg("cw", seqState.clientWidth), fmt::arg("pw", seqState.portWidth));

      y++, label++, index++;
    }
  }

  void View::drawConnections() {
    bool picking = mode == Mode::pickConnection;
    bool confirming = mode == Mode::confirmDisconnection;

    int y = connectionsRow;

    term.clearLine(y++);
    std::cout << boxCornerTL << boxHorizontal << " Connections ";
    for (auto i = 2*(seqState.clientWidth + seqState.portWidth) - 4; i; --i)
      std::cout << boxHorizontal;
    term.clearLine(y++);
    std::cout << boxVertical;

    char label = 'a';
    size_t index = 0;

    for (auto& c : seqState.connections) {
      term.clearLine(y);
      std::cout << boxVertical << ' ';

      if (picking && index == selectedConnection) std::cout << Term::Style::inverse;
      if (confirming)
        std::cout << (index == selectedConnection ? Term::Style::bold : Term::Style::dim);

      fmt::print("{}{} {} --> {}",
        picking ? label : ' ',
        picking ? ')' : ' ',
        c.sender, c.dest);

      y++, label++, index++;
    }
  }

  void View::drawPrompt() {
    const char* line1 = "";

    switch (mode) {
      case Mode::menu:
        line1 = "Q)uit, C)connect, D)isconnect";
        break;

      case Mode::pickSender:
        line1 = "Use arrows to pick a sender and hit return, or type a letter";
        break;

      case Mode::pickDest:
        line1 = "Now pick a dest the same way";
        break;

      case Mode::confirmConnection:
        line1 = "Confirm with return, or cancel with ESC";
        break;

      case Mode::pickConnection: {
        line1 = "Use arrows to pick a connection and hit return, or type a letter";
        break;
      }

      case Mode::confirmDisconnection:{
        line1 = "Confirm with return, or cancel with ESC";
        break;
      }

      case Mode::quit:
        line1 = "Quitting...";
        break;
    }
    term.clearLine(promptRow);
    std::cout << "  >> " << line1;
    term.clearLine(promptRow + 1);
    if (message.length() > 0)
      std::cout << "  ** " << message;
    term.moveCursor(promptRow, 1);
  }

  void View::setMessage(const std::string& s) {
    message = s;
    promptDirty = true;
  }

  void View::backup(bool fully = false) {
    switch (mode) {
      case Mode::menu:        break;
      case Mode::pickSender:
        mode = Mode::menu;
        portsDirty = true;
        break;
      case Mode::pickDest:
        mode = Mode::pickSender;
        portsDirty = true;
        break;
      case Mode::confirmConnection:
        mode = Mode::pickDest;
        portsDirty = true;
        break;
      case Mode::pickConnection:
        mode = Mode::menu;
        connectionsDirty = true;
        break;
      case Mode::confirmDisconnection:
        mode = Mode::pickConnection;
        connectionsDirty = true;
        break;
      default:
        mode = Mode::menu;
    }
    if (fully) mode = Mode::menu;
    promptDirty = true;
  }

  bool View::handleGlobalEvent(const Term::Event& ev) {
    switch (ev.type) {
      case Term::EventType::Char: {
        switch (ev.character) {
          case '\x03':      // control-c
            mode = Mode::quit;
            return true;

          case '\x1b':      // escape
            backup(true);
            return true;

          default:
            break;
        }
        break;
      }

      case Term::EventType::Key: {
        switch (ev.key) {
          case Term::Key::Left:
            backup();
            return true;
          default:
            break;
        }
        break;
      }

      case Term::EventType::Resize: {
        layout();
        return true;
      }

      default:
        break;
    }
    return false;
  }

  bool View::handleMenuEvent(const Term::Event& ev) {
    if (ev.type == Term::EventType::Char) {
      switch (ev.character) {
        case 'C':
        case 'c':
          mode = Mode::pickSender;
          promptDirty = portsDirty = true;
          return true;

        case 'D':
        case 'd':
        case 'X':
        case 'x':
          mode = Mode::pickConnection;
          promptDirty = connectionsDirty = true;
          return true;

        case 'Q':
        case 'q':
          mode = Mode::quit;
          return true;

        case 'R':
        case 'r':
          seqState.refresh();
          layout();
          return true;
      }
    }
    return false;
  }

  bool View::handlePortPickerEvent(const Term::Event& ev) {
    std::size_t* selector;
    using FilterFunc = bool (Address::*)() const;
    FilterFunc filter;
    const char* typeString;

    Mode nextMode;

    promptDirty = true;
    portsDirty = true;

    switch (mode) {
      case Mode::pickSender:
        selector = &selectedSender;
        filter = &Address::canBeSender;
        typeString = "sender";
        nextMode = Mode::pickDest;
        break;

      case Mode::pickDest:
        selector = &selectedDest;
        filter = &Address::canBeDest;
        typeString = "destination";
        nextMode = Mode::confirmConnection;
        break;

      default:
        return false;
    }

    switch (ev.type) {
      case Term::EventType::Char: {
        auto& c = ev.character;

        size_t pick;
        if ('A' <= c && c <= 'Z')       pick = c - 'A';
        else if ('a' <= c && c <= 'z')  pick = c - 'a';
        else if (ev.character == '\r'
              || ev.character == '\t')  pick = *selector;
        else                            return false;

        if (0 <= pick && pick < seqState.numPorts) {
          if ((seqState.ports[pick].*filter)()) {
            *selector = pick;
            mode = nextMode;
            return true;
          }
          else {
            setMessage(fmt::format("That port cannot be a {}", typeString));
          }
        }

        return false;
      }

      case Term::EventType::Key: {
        switch (ev.key) {
          case Term::Key::Down:
            forwardSelection(*selector, seqState.ports, filter);
            break;
          case Term::Key::Up:
            backwardSelection(*selector, seqState.ports, filter);
            break;
          default:
            return false;
        }
        return true;
      }

      default:
        break;
    }

    return false;
  }

 bool View::handleConnectionPickerEvent(const Term::Event& ev) {
    promptDirty = true;
    connectionsDirty = true;

    switch (ev.type) {
      case Term::EventType::Char: {
        auto& c = ev.character;

        size_t pick;
        if ('A' <= c && c <= 'Z')       pick = c - 'A';
        else if ('a' <= c && c <= 'z')  pick = c - 'a';
        else if (ev.character == '\r'
              || ev.character == '\t')  pick = selectedConnection;
        else                            return false;

        if (0 <= pick && pick < seqState.numConnections) {
          selectedConnection = pick;
          mode = Mode::confirmDisconnection;
          return true;
        }
        return false;
      }

      case Term::EventType::Key: {
        size_t pick = selectedConnection;
        switch (ev.key) {
          case Term::Key::Down:                 pick += 1;  break;
          case Term::Key::Up:     if (pick > 0) pick -= 1;  break;
          default:
            return false;
        }
        if (pick >= seqState.numConnections)
          pick = seqState.numConnections - 1;
        selectedConnection = pick;
        return true;
      }

      default:
        break;
    }

    return false;
  }

  bool View::handleConfirmEvent(const Term::Event& ev) {
    if (ev.type == Term::EventType::Char) {
      if (ev.character == '\r' || ev.character == '\t') {

        switch (mode) {
          case Mode::confirmConnection: {
            setMessage(fmt::format("Would connect {} --> {}",
              seqState.ports[selectedSender],
              seqState.ports[selectedDest]));
            break;
          }

          case Mode::confirmDisconnection: {
            auto& conn = seqState.connections[selectedConnection];
            setMessage(fmt::format("Would disconnect {} -x-> {}",
              conn.sender, conn.dest));
            break;
          }

          default:
            return false;
        }
        mode = Mode::menu;
        seqState.refresh();
        layout();
        return true;
      }
    }
    return false;
  }

  void View::handleEvent(const Term::Event& ev) {
    bool handled = false;

    if (handleGlobalEvent(ev)) {
      if (handled) debugMessage("handled in handleGlobalEvent");
      return;
    }

    switch (mode) {
      case Mode::menu:
        handled = handleMenuEvent(ev);
        if (handled) debugMessage("handled in handleMenuEvent");
        break;

      case Mode::pickSender:
      case Mode::pickDest:
        handled = handlePortPickerEvent(ev);
        if (handled) debugMessage("handled in handlePortPickerEvent");
        break;

      case Mode::pickConnection:
        handled = handleConnectionPickerEvent(ev);
        if (handled) debugMessage("handled in handleConnectionPickerEvent");
        break;

      case Mode::confirmConnection:
      case Mode::confirmDisconnection:
        handled = handleConfirmEvent(ev);
        if (handled) debugMessage("handled in handleConfirmEvent");
        break;

      case Mode::quit:
        return;
    }

    if (!handled) {
      switch (ev.type) {
        case Term::EventType::Char:
          debugMessage(fmt::format("Unassigned character {:?}", ev.character));
          break;

        case Term::EventType::Key:
          debugMessage(fmt::format("Unassigned key {}", static_cast<int>(ev.key)));
          break;

        case Term::EventType::UnknownEscapeSequence:
          debugMessage(fmt::format("Unknown escape sequence: {:?}", ev.unknown));
          break;

        default:
          debugMessage(fmt::format("Unknown event type {}", int(ev.type)));
          break;
      }
    }
  }

  void View::debugMessage(const std::string& s) {
    static size_t i = 0;
    static int n = 1;

    term.clearLine(i + 1);
    fmt::print("{:3}: {}", n, s);
    i = (i + 1) % 15;
    n += 1;
  }

  void View::run() {
    mode = Mode::menu;
    selectedSender = 0;
    selectedDest = 0;
    selectedConnection = 0;

    layout();

    while (mode != Mode::quit) {
      render();

      Term::Event ev = term.getEvent();
      if (ev.type == Term::EventType::None) continue;

      setMessage("");
      handleEvent(ev);
      debugMessage(fmt::format("in mode {}", int(mode)));
    }

  }
}

namespace Client {

  void viewCommand() {
    Term term;

    if(!term.good()) {
      listCommand();
      return;
    }

    View view(term);
    view.run();
  }

}