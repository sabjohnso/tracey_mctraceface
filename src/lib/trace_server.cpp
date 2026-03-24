#include <tracey_mctraceface/trace_server.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace tracey_mctraceface {

  namespace {

    auto
    read_trace_file(const std::filesystem::path& path) -> std::vector<char> {
      std::ifstream f(path, std::ios::binary | std::ios::ate);
      if (!f) return {};
      auto size = f.tellg();
      f.seekg(0);
      std::vector<char> data(static_cast<std::size_t>(size));
      f.read(data.data(), size);
      return data;
    }

    void
    send_all(int fd, const std::string& data) {
      std::size_t sent = 0;
      while (sent < data.size()) {
        auto n =
          ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
      }
    }

    void
    send_all(int fd, const std::vector<char>& data) {
      std::size_t sent = 0;
      while (sent < data.size()) {
        auto n =
          ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
      }
    }

    void
    handle_request(int client_fd, const std::vector<char>& trace_data) {
      // Read the request (we only need the first line)
      std::array<char, 4096> buf{};
      auto n = ::recv(client_fd, buf.data(), buf.size() - 1, 0);
      if (n <= 0) {
        ::close(client_fd);
        return;
      }
      buf[n] = '\0';
      std::string_view request(buf.data(), static_cast<std::size_t>(n));

      // CORS preflight
      if (request.starts_with("OPTIONS")) {
        std::string response = "HTTP/1.1 204 No Content\r\n"
                               "Access-Control-Allow-Origin: *\r\n"
                               "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                               "Access-Control-Allow-Headers: *\r\n"
                               "Content-Length: 0\r\n"
                               "\r\n";
        send_all(client_fd, response);
        ::close(client_fd);
        return;
      }

      // Serve the trace file
      if (request.starts_with("GET")) {
        auto header = "HTTP/1.1 200 OK\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Content-Type: application/octet-stream\r\n"
                      "Content-Length: " +
                      std::to_string(trace_data.size()) +
                      "\r\n"
                      "\r\n";
        send_all(client_fd, header);
        send_all(client_fd, trace_data);
        ::close(client_fd);
        return;
      }

      // Unknown request
      std::string response = "HTTP/1.1 400 Bad Request\r\n\r\n";
      send_all(client_fd, response);
      ::close(client_fd);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    volatile sig_atomic_t server_running = 1;

    void
    server_sigint_handler(int /*sig*/) {
      server_running = 0;
    }

  } // namespace

  void
  serve_trace(const std::filesystem::path& trace_path, std::uint16_t port) {
    auto trace_data = read_trace_file(trace_path);
    if (trace_data.empty()) {
      std::cerr << "error: could not read " << trace_path << '\n';
      return;
    }

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
      std::cerr << "error: socket() failed: " << std::strerror(errno) << '\n';
      return;
    }

    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (
      ::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      std::cerr << "error: bind() failed on port " << port << ": "
                << std::strerror(errno) << '\n';
      ::close(server_fd);
      return;
    }

    if (::listen(server_fd, 4) < 0) {
      std::cerr << "error: listen() failed: " << std::strerror(errno) << '\n';
      ::close(server_fd);
      return;
    }

    // Open Perfetto UI in the default browser
    auto url = "https://ui.perfetto.dev/#!/?url=http://127.0.0.1:" +
               std::to_string(port) + "/trace";
    std::cerr << "Serving trace at http://127.0.0.1:" << port << "/\n";
    std::cerr << "Opening " << url << "\n";
    std::cerr << "Press Ctrl+C to stop.\n";

    auto cmd = "xdg-open '" + url + "' 2>/dev/null &";
    std::system(cmd.c_str());

    // Handle SIGINT for clean shutdown
    struct sigaction sa{};
    sa.sa_handler = server_sigint_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    server_running = 1;

    while (server_running != 0) {
      // Use a timeout so we can check server_running
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(server_fd, &readfds);
      timeval tv{.tv_sec = 1, .tv_usec = 0};

      auto ready = ::select(server_fd + 1, &readfds, nullptr, nullptr, &tv);
      if (ready <= 0) continue;

      int client_fd = ::accept(server_fd, nullptr, nullptr);
      if (client_fd < 0) continue;

      handle_request(client_fd, trace_data);
    }

    std::cerr << "\nStopping server.\n";
    ::close(server_fd);
  }

} // namespace tracey_mctraceface
