#include <tracey_mctraceface/trace_server.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
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

    // Local HTML page that:
    // 1. Fetches /trace from the same localhost origin (no CORS issues)
    // 2. Opens Perfetto UI in a new tab
    // 3. Sends the trace via postMessage (Perfetto's supported API)
    auto
    opener_html() -> std::string {
      return R"(<!DOCTYPE html>
<html><body style="font-family:sans-serif; margin:2em;">
<h2>tracey_mctraceface</h2>
<p id="status">Loading trace...</p>
<button id="open" style="display:none; font-size:1.2em; padding:0.5em 1em; cursor:pointer;">
  Open in Perfetto
</button>
<script>
let traceBuffer = null;

async function loadTrace() {
  const status = document.getElementById('status');
  const btn = document.getElementById('open');
  try {
    const resp = await fetch('/trace');
    const blob = await resp.blob();
    traceBuffer = await blob.arrayBuffer();
    status.textContent = 'Trace loaded (' + (traceBuffer.byteLength / 1024).toFixed(0) + ' KB). Click to open:';
    btn.style.display = 'inline-block';
  } catch (e) {
    status.textContent = 'Error loading trace: ' + e.message;
  }
}

function openPerfetto() {
  const status = document.getElementById('status');
  const btn = document.getElementById('open');
  const win = window.open('https://ui.perfetto.dev');
  if (!win) { status.textContent = 'Popup still blocked. Allow popups for 127.0.0.1.'; return; }

  btn.style.display = 'none';
  status.textContent = 'Waiting for Perfetto to load...';

  const timer = setInterval(() => {
    win.postMessage('PING', 'https://ui.perfetto.dev');
  }, 200);

  window.addEventListener('message', (evt) => {
    if (evt.data !== 'PONG') return;
    clearInterval(timer);
    win.postMessage({
      perfetto: { buffer: traceBuffer, title: 'tracey_mctraceface', keepApiOpen: false }
    }, 'https://ui.perfetto.dev');
    status.textContent = 'Trace sent to Perfetto. You can close this tab.';
  });
}

document.getElementById('open').addEventListener('click', openPerfetto);
loadTrace();
</script>
</body></html>)";
    }

    void
    handle_request(int client_fd, const std::vector<char>& trace_data) {
      std::array<char, 4096> buf{};
      auto n = ::recv(client_fd, buf.data(), buf.size() - 1, 0);
      if (n <= 0) {
        ::close(client_fd);
        return;
      }
      buf[n] = '\0';
      std::string_view request(buf.data(), static_cast<std::size_t>(n));

      // Extract path from "GET /path HTTP/1.1"
      auto path_start = request.find(' ');
      auto path_end = request.find(' ', path_start + 1);
      auto path = (path_start != std::string_view::npos &&
                   path_end != std::string_view::npos)
                    ? request.substr(path_start + 1, path_end - path_start - 1)
                    : std::string_view("/");

      if (request.starts_with("GET") && path.starts_with("/trace")) {
        // Serve the raw trace binary
        auto header = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/octet-stream\r\n"
                      "Content-Length: " +
                      std::to_string(trace_data.size()) + "\r\n\r\n";
        send_all(client_fd, header);
        send_all(client_fd, trace_data);
      } else if (request.starts_with("GET")) {
        // Serve the opener page (fetches /trace, posts to Perfetto)
        auto html = opener_html();
        auto header = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: " +
                      std::to_string(html.size()) + "\r\n\r\n";
        send_all(client_fd, header);
        send_all(client_fd, html);
      } else {
        send_all(client_fd, std::string("HTTP/1.1 400 Bad Request\r\n\r\n"));
      }

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

    auto url = "http://127.0.0.1:" + std::to_string(port);
    std::cerr << "Serving trace at " << url << "\n";
    std::cerr << "Opening browser...\n";
    std::cerr << "Press Ctrl+C to stop.\n";

    // Open browser without shell injection risk
    auto browser_pid = fork();
    if (browser_pid == 0) {
      // Redirect stderr to /dev/null
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
      }
      execlp("xdg-open", "xdg-open", url.c_str(), nullptr);
      _exit(127);
    }

    struct sigaction sa{};
    sa.sa_handler = server_sigint_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    server_running = 1;

    while (server_running != 0) {
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
