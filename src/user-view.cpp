#include "user.h"

#include <fmt/format.h>
#include <sys/epoll.h>

#include "msg.h"
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
    Menu,

    PickSender,
    PickDest,
    ConfirmConnection,

    PickConnection,
    ConfirmDisconnection,

    Quit,
  };

  enum class Command {
    None,

    Connect,
    UndoConnect,
    RedoConnect,
    Disconnect,
    UndoDisconnect,
    RedoDisconnect,
  };

  enum class FDSource : uint32_t {
    Signal,
    Seq,
    Term,
  };

  void addFDToEpoll(int epollFD, int fd, FDSource src) {
    struct epoll_event evt;
    evt.events = EPOLLIN | EPOLLERR;
    evt.data.u32 = (uint32_t)src;

    if (epoll_ctl(epollFD, EPOLL_CTL_ADD, fd, &evt) != 0)
      throw Msg::system_error("Failed adding to epoll");
  }

  struct View {
    Term& term;
    SeqSnapshot seqState;

    View(Term& t) : term(t) { }

    std::size_t topRowPorts;
    std::size_t topRowConnections;
    std::size_t topRowPrompt;

    void layout();

    std::size_t selectedSender = 0;
    std::size_t selectedDest = 0;
    std::size_t selectedConnection = 0;

    std::string message;
    void setMessage(const std::string&);
    template <typename... T>
    void setMessage(const char* format, const T&... args)
      { setMessage(fmt::vformat(format, fmt::make_format_args(args...))); }

    bool dirtyPorts = false;
    bool dirtyConnections = false;
    bool dirtyPrompt = false;

    void drawHeader(const char* title, size_t row, size_t contentWidth);

    void drawPorts();
    void drawConnections();
    void drawPrompt();

    void render();


    Mode mode = Mode::Menu;
    Mode priorMode();
    void gotoMode(Mode);

    Command lastCommand = Command::None;
    Address lastSender;
    Address lastDest;
    bool validateConnect(const Address&, const Address&);
    bool validateDisconnect(const Address&, const Address&);
    void performCommand(Command, const Address&, const Address&);
    void undoCommand();
    void validateUndo();
    bool canUndo() const;
    bool canRedo() const;

    void handleEvent(const Term::Event& ev);
    bool handleGlobalEvent(const Term::Event& ev);
    bool handleMenuEvent(const Term::Event& ev);
    bool handlePortPickerEvent(const Term::Event& ev);
    bool handleConnectionPickerEvent(const Term::Event& ev);
    bool handleConfirmEvent(const Term::Event& ev);

    void debugMessage(const std::string&);
    template <typename... T>
    void debugMessage(const char* format, const T&... args)
      { debugMessage(fmt::vformat(format, fmt::make_format_args(args...))); }

    void run();
  };


  void View::layout() {
    auto numPorts = seqState.ports.size();
    auto numConnections = seqState.connections.size();
    if (numPorts == 0) numPorts = 1;
    if (numConnections == 0) numConnections = 1;

    auto portsHeight       = 2 + numPorts + 1;
    auto connectionsHeight = 2 + numConnections + 1;
    auto promptHeight      = 2;

    topRowPrompt = term.rows() + 1 - promptHeight;
    topRowConnections = topRowPrompt - connectionsHeight;
    topRowPorts = topRowConnections - portsHeight;

    dirtyPorts = true;
    dirtyConnections = true;
    dirtyPrompt = true;

    if (selectedSender >= numPorts)   selectedSender = numPorts - 1;
    if (selectedDest >= numPorts)     selectedDest = numPorts - 1;
    if (selectedConnection >= numConnections)
        selectedConnection = numConnections - 1;

    term.clearDisplay();
  }

  void View::render() {
    if (dirtyPorts)       drawPorts();
    if (dirtyConnections) drawConnections();
    if (dirtyPrompt)      drawPrompt();

    std::cout.flush();
    dirtyPorts = dirtyConnections = dirtyPrompt = false;
  }

  void View::drawHeader(const char* title, size_t row, size_t contentWidth) {
    if (contentWidth < 40) contentWidth = 40;

    term.clearLine(row);
    std::cout << boxCornerTL;
    for (auto i = contentWidth; i > 1; --i) std::cout << boxHorizontal;
    term.moveCursor(row, 3);
    std::cout << ' ' << title << ' ';
    term.clearLine(row+1);
    std::cout << boxVertical;
  }

  void View::drawPorts() {
    bool picking = false;
    bool confirming = false;
    size_t s1 = seqState.ports.size();
    size_t s2 = seqState.ports.size();
    size_t inv = seqState.ports.size();
    auto func = &Address::canBeSender;

    switch (mode) {
      case Mode::PickSender:
        picking = true;
        s1 = selectedSender;
        inv = selectedSender;
        break;
      case Mode::PickDest:
        picking = true;
        s1 = selectedSender;
        s2 = selectedDest;
        inv = selectedDest;
        func = &Address::canBeDest;
        break;
      case Mode::ConfirmConnection:
        confirming = true;
        s1 = selectedSender;
        s2 = selectedDest;
        break;
      default:
        break;
    }

    drawHeader("Ports", topRowPorts,
      seqState.clientWidth + seqState.portWidth + 20);

    int y = topRowPorts + 2;
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
        (isS1 | isS2) ? "    " : "",  // shifts over the arrow is selected
        (isS1 | isS2) ? SeqSnapshot::dirStr(isS1, isS2)
                      : SeqSnapshot::addressDirStr(p),
        fmt::arg("cw", seqState.clientWidth), fmt::arg("pw", seqState.portWidth));

      y++, label++, index++;
    }
    if (index == 0) {
      term.clearLine(y);
      std::cout << boxVertical << ' '
        << Term::Style::dim << "   -- no ports --";
    }
  }

  void View::drawConnections() {
    bool picking = mode == Mode::PickConnection;
    bool confirming = mode == Mode::ConfirmDisconnection;

    drawHeader("Connections", topRowConnections,
      2*(seqState.clientWidth + seqState.portWidth) + 28);

    int y = topRowConnections + 2;
    char label = 'a';
    size_t index = 0;

    for (auto& c : seqState.connections) {
      term.clearLine(y);
      std::cout << boxVertical << ' ';

      if (picking && index == selectedConnection)
                                          std::cout << Term::Style::inverse;
      if (confirming) {
        if (index == selectedConnection)  std::cout << Term::Style::bold;
        else                              std::cout << Term::Style::dim;
      }
      fmt::print("{}{} {} --> {}",
        picking ? label : ' ',
        picking ? ')' : ' ',
        c.sender, c.dest);

      y++, label++, index++;
    }
    if (index == 0) {
      term.clearLine(y);
      std::cout << boxVertical << ' '
        << Term::Style::dim << "   -- no connections --";
    }
  }

  void View::drawPrompt() {
    const char* prompt = "";
    const char* extra = "";

    switch (mode) {
      case Mode::Menu:
        prompt = "Q)uit, C)onnect, D)isconnect";
        if (canUndo())        extra = ", U)ndo";
        else if (canRedo())   extra = ", R)edo";
        break;

      case Mode::PickSender:
        prompt = "Use arrows to pick a sender and hit return, or type a letter";
        break;

      case Mode::PickDest:
        prompt = "Now pick a dest the same way";
        break;

      case Mode::PickConnection: {
        prompt = "Use arrows to pick a connection and hit return, or type a letter";
        break;
      }

      case Mode::ConfirmConnection:
      case Mode::ConfirmDisconnection: {
        prompt = "Confirm with return, or cancel with ESC";
        break;
      }

      case Mode::Quit:
        prompt = "Quitting...";
        break;
    }
    term.clearLine(topRowPrompt);
    std::cout << "  >> " << prompt << extra;
    term.clearLine(topRowPrompt + 1);
    if (message.length() > 0)
      std::cout << "  ** " << message;
    term.moveCursor(topRowPrompt, 1);
  }

  void View::setMessage(const std::string& s) {
    message = s;
    dirtyPrompt = true;
  }

  void View::gotoMode(Mode newMode) {
    Mode modes[] = {mode, newMode};
    for (auto m : modes) {
      switch(m) {
        case Mode::Menu:                  break;
        case Mode::PickSender:
        case Mode::PickDest:
        case Mode::ConfirmConnection:     dirtyPorts = true; break;
        case Mode::PickConnection:
        case Mode::ConfirmDisconnection:  dirtyConnections = true; break;
        case Mode::Quit:                  break;
      }
    }
    dirtyPrompt = true;
    mode = newMode;
  }

  Mode View::priorMode() {
    switch (mode) {
      case Mode::Menu:                    return Mode::Menu;
      case Mode::PickSender:              return Mode::Menu;
      case Mode::PickDest:                return Mode::PickSender;
      case Mode::ConfirmConnection:       return Mode::PickDest;
      case Mode::PickConnection:          return Mode::Menu;
      case Mode::ConfirmDisconnection:    return Mode::PickConnection;
      case Mode::Quit:                    return Mode::Quit;
    }
    return mode; // never reached, appeases the compiler
  }

  bool View::validateConnect(const Address& s, const Address& d) {
    return seqState.addressStillValid(s)
      && seqState.addressStillValid(d)
      && !seqState.hasConnectionBetween(s, d);
  }
  bool View::validateDisconnect(const Address& s, const Address& d) {
    return seqState.addressStillValid(s)
      && seqState.addressStillValid(d)
      && seqState.hasConnectionBetween(s, d);
  }

  void View::performCommand(Command c, const Address& s, const Address& d) {
    switch (c) {
      case Command::None:
        break;

      case Command::Connect:
      case Command::RedoConnect:
      case Command::UndoDisconnect: {
        if (!validateConnect(s, d)) {
          setMessage("Already connected: {} --> {}", s, d);
          lastCommand = Command::None;
          break;
        }
        seqState.seq.connect(s.addr, d.addr);
        setMessage("Connected {} --> {}", s, d);
        lastCommand = c;
        lastSender = s;
        lastDest = d;
        break;
      }

      case Command::Disconnect:
      case Command::RedoDisconnect:
      case Command::UndoConnect: {
        if (!validateDisconnect(s, d)) {
          setMessage("Not connected: {} -x-> {}", s, d);
          lastCommand = Command::None;
          break;
        }
        seqState.seq.disconnect({s.addr, d.addr});
        setMessage("Disconnected {} -x-> {}", s, d);
        lastCommand = c;
        lastSender = s;
        lastDest = d;
        break;
      }
    }
  }

  void View::validateUndo() {
    switch (lastCommand) {
      case Command::None:
        break;
      case Command::Connect:
      case Command::RedoConnect:
      case Command::UndoDisconnect:
        // validating the potential disconnection a undo/redo would cause
        if (!validateDisconnect(lastSender, lastDest)) {
          lastCommand = Command::None;
          setMessage("");
        }
        break;
      case Command::Disconnect:
      case Command::RedoDisconnect:
      case Command::UndoConnect:
        // validating the potential connection a undo/redo would cause
        if (!validateConnect(lastSender, lastDest)) {
          lastCommand = Command::None;
          setMessage("");
        }
        break;
    }
  }

  void View::undoCommand() {
    switch (lastCommand) {
      case Command::None:
        break;
      case Command::Connect:
      case Command::RedoConnect:
        performCommand(Command::UndoConnect, lastSender, lastDest);
        break;
      case Command::UndoConnect:
        performCommand(Command::RedoConnect, lastSender, lastDest);
        break;
      case Command::Disconnect:
      case Command::RedoDisconnect:
        performCommand(Command::UndoDisconnect, lastSender, lastDest);
        break;
      case Command::UndoDisconnect:
        performCommand(Command::RedoDisconnect, lastSender, lastDest);
        break;
    }
  }

  bool View::canUndo() const {
    switch (lastCommand) {
      case Command::Connect:
      case Command::RedoConnect:
      case Command::Disconnect:
      case Command::RedoDisconnect:
        return true;
      default:
        return false;
    }
  }

  bool View::canRedo() const {
    switch (lastCommand) {
      case Command::UndoConnect:
      case Command::UndoDisconnect:
        return true;
      default:
        return false;
    }
  }

  bool View::handleGlobalEvent(const Term::Event& ev) {
    switch (ev.type) {
      case Term::EventType::Char: {
        switch (ev.character) {
          case '\x03':      // control-c
            gotoMode(Mode::Quit);
            return true;

          case '\x0c':      // control-l  - like in vi!
            layout();
            return true;

          case '\x1b':      // escape
            gotoMode(Mode::Menu);
            return true;

          default:
            break;
        }
        break;
      }

      case Term::EventType::Key: {
        switch (ev.key) {
          case Term::Key::Left:
            gotoMode(priorMode());
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
          lastCommand = Command::None;
          if (seqState.ports.empty())
            setMessage("Connecting nothingness // Yields nothingness");
          else
            gotoMode(Mode::PickSender);
          return true;

        case 'D':
        case 'd':
          lastCommand = Command::None;
          if (seqState.connections.empty())
            setMessage("Everything is disconnected // Be at peace");
          else
            gotoMode(Mode::PickConnection);
          return true;

        case 'Q':
        case 'q':
          gotoMode(Mode::Quit);
          return true;

        case 'R':
        case 'r':
          if (canRedo()) {
            undoCommand();
            return true;
          }
          break;

        case 'U':
        case 'u':
          if (canUndo()) {
            undoCommand();
            return true;
          }
          break;
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

    switch (mode) {
      case Mode::PickSender:
        selector = &selectedSender;
        filter = &Address::canBeSender;
        typeString = "sender";
        nextMode = Mode::PickDest;
        break;

      case Mode::PickDest:
        selector = &selectedDest;
        filter = &Address::canBeDest;
        typeString = "destination";
        nextMode = Mode::ConfirmConnection;
        break;

      default:
        return false;
    }

    dirtyPorts = true;

    switch (ev.type) {
      case Term::EventType::Char: {
        auto& c = ev.character;

        size_t pick;
        if ('A' <= c && c <= 'Z')       pick = c - 'A';
        else if ('a' <= c && c <= 'z')  pick = c - 'a';
        else if (ev.character == '\r'
              || ev.character == '\t')  pick = *selector;
        else                            return false;

        if (pick < seqState.ports.size()) {
          if ((seqState.ports[pick].*filter)()) {
            *selector = pick;
            gotoMode(nextMode);
            return true;
          }
          else {
            setMessage("That port cannot be a {}", typeString);
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
    dirtyConnections = true;

    switch (ev.type) {
      case Term::EventType::Char: {
        auto& c = ev.character;

        size_t pick;
        if ('A' <= c && c <= 'Z')       pick = c - 'A';
        else if ('a' <= c && c <= 'z')  pick = c - 'a';
        else if (ev.character == '\r'
              || ev.character == '\t')  pick = selectedConnection;
        else                            return false;

        if (pick < seqState.connections.size()) {
          selectedConnection = pick;
          gotoMode(Mode::ConfirmDisconnection);
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
        if (pick >= seqState.connections.size())
          pick = seqState.connections.size() - 1;
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
          case Mode::ConfirmConnection: {
            auto& sender = seqState.ports[selectedSender];
            auto& dest   = seqState.ports[selectedDest];
            performCommand(Command::Connect, sender, dest);
            break;
          }

          case Mode::ConfirmDisconnection: {
            auto& conn = seqState.connections[selectedConnection];
            performCommand(Command::Disconnect, conn.sender, conn.dest);
            break;
          }

          default:
            return false;
        }
        gotoMode(Mode::Menu);
        return true;
      }
    }
    return false;
  }

  void View::handleEvent(const Term::Event& ev) {
    bool handled = false;

    if (handleGlobalEvent(ev)) {
      // debugMessage("handled in handleGlobalEvent");
      return;
    }

    switch (mode) {
      case Mode::Menu:
        handled = handleMenuEvent(ev);
        // if (handled) debugMessage("handled in handleMenuEvent");
        break;

      case Mode::PickSender:
      case Mode::PickDest:
        handled = handlePortPickerEvent(ev);
        // if (handled) debugMessage("handled in handlePortPickerEvent");
        break;

      case Mode::PickConnection:
        handled = handleConnectionPickerEvent(ev);
        // if (handled) debugMessage("handled in handleConnectionPickerEvent");
        break;

      case Mode::ConfirmConnection:
      case Mode::ConfirmDisconnection:
        handled = handleConfirmEvent(ev);
        // if (handled) debugMessage("handled in handleConfirmEvent");
        break;

      case Mode::Quit:
        return;
    }

    if (false && !handled) {
      switch (ev.type) {
        case Term::EventType::Char:
          debugMessage("Unassigned character {:?}", ev.character);
          break;

        case Term::EventType::Key:
          debugMessage("Unassigned key {}", static_cast<int>(ev.key));
          break;

        case Term::EventType::UnknownEscapeSequence:
          debugMessage("Unknown escape sequence: {:?}", ev.unknown);
          break;

        default:
          debugMessage("Unknown event type {}", int(ev.type));
          break;
      }
    }
  }

  void View::debugMessage(const std::string& s) {
    const size_t debugAreaHeight = 15;
    static size_t row = 1;
    static int messageNumber = 1;

    term.clearLine(row);
    fmt::print("{:3}: {}", messageNumber, s);
    row = (row  % debugAreaHeight) + 1;
    term.clearLine(row); // clear the next line so the current one stands out
    messageNumber += 1;
  }

  void View::run() {
    seqState.refresh();

    mode = Mode::Menu;
    selectedSender = 0;
    selectedDest = 0;
    selectedConnection = 0;

    int epollFD = epoll_create1(0);
    if (epollFD == -1)
      throw Msg::system_error("epoll_create failed");

    seqState.seq.scanFDs(
      [&](int fd){ addFDToEpoll(epollFD, fd, FDSource::Seq); });
    term.scanFDs(
      [&](int fd){ addFDToEpoll(epollFD, fd, FDSource::Term); });

    layout();

    while (mode != Mode::Quit) {
      render();

      struct epoll_event evt;
      int nfds = epoll_wait(epollFD, &evt, 1, -1);
      if (nfds == -1) {
        if (errno == EINTR)
          evt.data.u32 = uint32_t(FDSource::Signal);
        else
          throw Msg::system_error("epoll_wait failed");
      }
      if (nfds == 0)
        continue;

      int et = -1;
      switch ((FDSource)evt.data.u32) {
        case FDSource::Seq:
          if (seqState.checkIfNeedsRefresh()) {
            seqState.refresh();
            validateUndo();
            layout();
            gotoMode(Mode::Menu);
          }
          break;

        case FDSource::Signal:
          // only caught signal is window resize, handled here
        case FDSource::Term: {
          Term::Event ev = term.getEvent();
          if (ev.type == Term::EventType::None) continue;
          et = int(ev.type);
          setMessage("");
          handleEvent(ev);
          break;
        }
      }

      if (false)
        debugMessage("in mode {}, after fd={} fdsource={} ev.type={}",
          int(mode), +evt.data.fd, +evt.data.u32, et);
    }
  }
}

namespace User {

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