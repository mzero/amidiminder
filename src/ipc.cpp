#include "ipc.h"

#include <stdio.h>
#include <sstream>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "files.h"
#include "msg.h"

namespace {

  void removeSocketFile() {
    auto path = Files::controlSocketPath();

    int err = remove(path.c_str());
    if (err == -1 && (errno != 0 && errno != ENOENT))
      Msg::error("Couldn't remove socket {}", path);
      // This doesn't throw as we don't want to obscure any error that is
      // causing the code to shutdown.
  }

  int makeSocket(bool server) {
    if (server) Files::initializeAsService();
    else        Files::initializeAsClient();

    int sockType = server ? (SOCK_STREAM | SOCK_NONBLOCK) : SOCK_STREAM;

    int sockFD = socket(AF_UNIX, sockType, 0);
    if (sockFD == -1)
      throw Msg::system_error("Couldn't create IPC socket");

    auto path = Files::controlSocketPath();

    if (server) removeSocketFile();

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (path.length() > sizeof(addr.sun_path) - 1)
      throw Msg::runtime_error("Socket path is too long: {}", path);

    if (server) {
      auto oldmask = umask(0007); // allow group access (usually audio)
      if (bind(sockFD, (const struct sockaddr*)&addr, sizeof(addr)) != 0)
        throw Msg::system_error("Couldn't bind socket to path {}", path);
      umask(oldmask);

      if (listen(sockFD, 2) != 0)   // don't need a long backlog
        throw Msg::system_error("Couldn't listen to socket path {}", path);
    }
    else {
      if (connect(sockFD, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
        auto errStr = strerror(errno);
        std::cerr << "Couldn't connect to the amidiminder daemon.\n"
          "\n"
          "Use systemctl to check or start it:\n"
          "    systemctl status amidiminder.service\n"
          "    systemctl start amidiminder.service\n"
          "\n"
          "(While trying to connect to the socket path:\n"
          "    " << path << "\n"
          "    got the error: " << errStr << ")\n";
        std::cerr.flush();
        throw Msg::runtime_error("");
      }
    }

    return sockFD;
  }

  const char optionsDelimiter = ',';
}



namespace IPC {

  Socket::Socket(int fd) : sockFD(fd) { }
  Socket::~Socket()                   { close(); }

  Socket::Socket(Socket&& other) noexcept : sockFD(other.sockFD)
    { other.invalidate(); }
  Socket& Socket::operator=(Socket&& other) noexcept {
    if (&other != this) {
      close();
      sockFD = other.sockFD;
      other.invalidate();
    }
    return *this;
  }

  bool Socket::valid()      { return sockFD != -1; }
  void Socket::invalidate() { sockFD = -1; }
  void Socket::close() {
    if (valid()) ::close(sockFD);
    invalidate();
  }

  void Socket::write(const char* buf, size_t count) {
    while (count) {
      auto n = ::write(sockFD, buf, count);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
          n = 0;
        else
          throw SocketError("Socket write failed");
      }
      buf += n;
      count -= n;
    }
  }

  void Socket::sendLine(const std::string& s) {
    write(s.data(), s.length());

    const char newline = '\n';
    write(&newline, 1);
  }

  std::string Socket::receiveLine() {
    std::string s;
    s.reserve(80);

    while (true) {
      if (s.length() >= 80) return "error";

      char ch;
      int n = read(sockFD, &ch, 1);

      if (n < 0) {
        if (errno == EAGAIN || errno == EINTR)
          continue;
        else
          throw SocketError("Socket receive line failed");
      }
      if (n == 0 || ch == '\n') return s;
      s.push_back(ch);
    }
  }

  void Socket::sendFile(std::istream& in) {
    char buffer[1024];

    while (in.good()) {
      in.read(buffer, sizeof(buffer));
      write(buffer, in.gcount());
    }
  }

  void Socket::receiveFile(std::ostream& out) {
    char buffer[1024];

    while(true) {
      int n = read(sockFD, &buffer, sizeof(buffer));

      if (n < 0) {
        if (errno == EAGAIN || errno == EINTR)
          continue;
        else
          throw SocketError("Socket receive file failed");
      }
      if (n == 0) return;

      out.write(buffer, n);
    }
  }


  SocketError::SocketError(const char* what)
    : std::system_error(errno, std::generic_category(), what)
    { }


  Client::Client() : Socket(makeSocket(false))  { }
  Client::~Client()                             { }

  void Client::sendCommand(const std::string& cmd) {
    static Options noOptions;
    sendCommandAndOptions(cmd, noOptions);
  }
  void Client::sendCommandAndOptions(const std::string& cmd, const Options& opts) {
    std::ostringstream line;
    line << cmd;
    for (auto o : opts)
      line << optionsDelimiter << o;
    sendLine(line.str());
  }
  void Client::sendFile(std::istream& f)            { Socket::sendFile(f); }
  void Client::receiveFile(std::ostream& f)         { Socket::receiveFile(f); }


  Connection::Connection(int fd) : Socket(fd) { }
  Connection::~Connection()                   { }

  Connection::Connection(Connection&& other) noexcept : Socket(std::move(other)) { }
  Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other)
      *this = std::move(other);
    return *this;
  }

  std::string Connection::receiveCommand() {
    return receiveCommandAndOptions().first;
  }
  std::pair<std::string, Options> Connection::receiveCommandAndOptions() {
    std::string line = receiveLine();
    Msg::output("Received client command: {}", line);

    std::istringstream is(line);
    std::string cmd;
    Options opts;
    for (std::string word; std::getline(is, word, optionsDelimiter);)
      if (!word.empty()) {
        if (cmd.empty())  cmd = word;
        else              opts.push_back(word);
      }

    return std::make_pair(cmd, opts);
  }
  void Connection::sendFile(std::istream& f)        { Socket::sendFile(f); }
  void Connection::receiveFile(std::ostream& f)     { Socket::receiveFile(f); }


  Server::Server() : Socket(makeSocket(true)) { }
  Server::~Server()                           { removeSocketFile(); }

  void Server::scanFDs(std::function<void(int)> fn) {
    fn(sockFD);
  }

  std::optional<Connection> Server::accept() {
    int connFD = ::accept(sockFD, NULL, NULL);
    if (connFD == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {};
      }

      throw Msg::system_error("Accepting a connection failed");
    }

    return Connection(connFD);
  }
}