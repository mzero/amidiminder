#include "term.h"

#include <csignal>
#include <exception>
#include <fmt/format.h>
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
  volatile std::sig_atomic_t pendingResize = false;
  void sigwinch_handler(int /* signal */) { pendingResize = true; }
}

Term::Term() {
  _good = false;
  _rows = 0;
  _cols = 0;

  if (!isatty(STDIN_FILENO)) throw std::runtime_error("stdin isn't a tty");
  if (!isatty(STDOUT_FILENO)) throw std::runtime_error("stdout isn't a tty");

  int err;

  struct termios t;
  err = tcgetattr(STDOUT_FILENO, &t);
  if (err != 0) throw std::system_error(errno, std::generic_category(), "getting term attr");
  _original_termios = t;
  cfmakeraw(&t);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  err = tcsetattr(STDOUT_FILENO, TCSAFLUSH, &t);
  if (err != 0) throw std::system_error(errno, std::generic_category(), "setting term attr");

  pendingResize = false;
  std::signal(SIGWINCH, sigwinch_handler);
  fetchWindowSize();

  std::cout <<
    "\x1b[?1049h"   // use alternate buffer, saving cursor first
    "\x1b[2J"       // erase whole screen
    "\x1b[?7l"      // turn off wrapping
    "\x1b[0m"       // reset style
    ;
  std::cout.flush();

  _good = true;
}

Term::~Term() {
  if (!_good) return;

  std::signal(SIGWINCH, SIG_DFL);

  std::cout <<
    "\x1b[?1049l"   // use original buffer, retore cursor
    ;

  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &_original_termios);
}

void Term::scanFDs(std::function<void(int)> fn) { fn(STDIN_FILENO); }

void Term::fetchWindowSize() {
  struct winsize w;
  int err = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  if (err != 0)
    throw std::system_error(errno, std::generic_category(), "getting term size");
  _rows = w.ws_row;
  _cols = w.ws_col;
}

void Term::moveCursor(int row, int col){
  fmt::print("\x1b[{};{}H", row, col);
}

void Term::clearLine(int row){
  moveCursor(row, 1);
  std::cout << "\x1b[2K"      // clear whole line
            << Style::reset;
}

void Term::clearDisplay(){
  moveCursor(1, 1);
  std::cout << "\x1b[2J"      // clear whole display
            << Style::reset;
}


const char* Term::Style::reset    = "\x1b[0m";
const char* Term::Style::bold     = "\x1b[1m";
const char* Term::Style::dim      = "\x1b[2m";
const char* Term::Style::inverse  = "\x1b[7m";


void Term::remainingInput(const std::string& s, std::size_t pos) {
  _pending_input = s.substr(pos);
}

std::string Term::readInput() {
  std::ostringstream o;
  o << _pending_input;
  _pending_input.clear();

  char c;
  while (true) {
    int n = read(STDIN_FILENO, &c, 1);
    if (n < 0) throw std::system_error(errno, std::generic_category(), "error in read");
    if (n == 0) break;
    o.put(c);
  };

  return o.str();
}


Term::Event Term::getEvent() {
  Event ev;
  ev.type = EventType::None;
  ev.shift = ev.alt = ev.control = ev.meta = false;

  if (pendingResize) {
    pendingResize = false;
    fetchWindowSize();
    ev.type = EventType::Resize;
    return ev;
  }

  std::string s = readInput();

  if (s.empty()) {
    ev.type = EventType::None;
    return ev;
  };

  if (s.length() == 1 || s[0] != '\x1b') {
    remainingInput(s, 1);
    ev.type = EventType::Char;
    ev.character = s[0];
    return ev;
  }

  if (s == "\x1b\x1b") {
    remainingInput(s, 2);
    ev.type = EventType::Char;
    ev.character = '\x1b';
    return ev;
  };

  if (s.length() == 2 || s[1] != '[') {
    remainingInput(s, 2);
    ev.type = EventType::Char;
    ev.alt = true;
    ev.character = s[1];
    return ev;
  }

  if (s.length() == 3) {
    switch (s[2]) {
      case 'A':   ev.key = Term::Key::Up;     break;
      case 'B':   ev.key = Term::Key::Down;   break;
      case 'C':   ev.key = Term::Key::Right;  break;
      case 'D':   ev.key = Term::Key::Left;   break;
      case 'F':   ev.key = Term::Key::Home;   break;
      case 'H':   ev.key = Term::Key::End;    break;
      default:
        goto unknown;
    }
    ev.type = EventType::Key;
    return ev;
  }

unknown:
  ev.type = EventType::UnknownEscapeSequence;
  ev.unknown = s;
  return ev;
}

