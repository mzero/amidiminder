#ifndef _INCLUDE_IPC_H_
#define _INCLUDE_IPC_H_

#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>

namespace IPC {
  class Socket {
    private:
      Socket(int fd);
      ~Socket();

      // Can't copy a Socket...
      Socket(const Socket&) = delete;
      Socket& operator=(const Socket&) = delete;

      // ...but you can move it.
      Socket(Socket&&) noexcept;
      Socket& operator=(Socket&&) noexcept;

      bool valid();
      void close();
      void invalidate();

      void write(const char*, size_t);
      void sendLine(const std::string&);
      std::string receiveLine();

      void sendFile(std::istream&);
      void receiveFile(std::ostream&);

      int sockFD;

    friend class Client;
    friend class Server;
    friend class Connection;
  };

  using Options = std::vector<std::string>;

  class Client : Socket {
    public:
      Client();
      ~Client();

      void sendCommand(const std::string&);
      void sendCommandAndOptions(const std::string&, const Options&);
      void sendFile(std::istream&);
      void receiveFile(std::ostream&);
  };

  class Connection : Socket {
    private:
      Connection(int fd);
    public:
      ~Connection();

      // Can't copy a Connection...
      Connection(const Connection&) = delete;
      Connection& operator=(const Connection&) = delete;

      // ...but you can move it.
      Connection(Connection&&) noexcept;
      Connection& operator=(Connection&&) noexcept;

      std::string receiveCommand();
      std::pair<std::string, Options> receiveCommandAndOptions();
      void sendFile(std::istream&);
      void receiveFile(std::ostream&);

    friend class Server;
  };

  class Server : Socket {
    public:
      Server();
      ~Server();

      void scanFDs(std::function<void(int)>);
      std::optional<Connection> accept();
  };
}

#endif // _INCLUDE_IPC_H_
