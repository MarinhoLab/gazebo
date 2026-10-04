/*
 * Copyright (c) 2026 Murilo Marques Marinho
 *
 *    This file is part of gazebo (https://github.com/MarinhoLab/gazebo).
 *
 *    gazebo (https://github.com/MarinhoLab/gazebo) is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 2.1 of the License, or
 *    (at your option) any later version.
 *
 *    gazebo (https://github.com/MarinhoLab/gazebo) is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License
 *    along with gazebo (https://github.com/MarinhoLab/gazebo).  If not, see <https://www.gnu.org/licenses/>.
 */

// Proof of concept: native macOS gz-transport <-> ONE fixed TCP port.
//
// The Mac is always the TCP *client*. That is deliberate: a Mac-initiated
// connection to a published container port is the only direction that crosses
// the Docker Desktop boundary reliably, and it is full-duplex, so one connection
// carries both directions.
//
//   gzif --connect 127.0.0.1:9100 --sub /world/empty/clock --sub /stats
//
// Frames: JSON header line, 4-byte big-endian payload length, payload bytes.
#include <gz/transport/Node.hh>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
std::mutex g_tx;
int g_fd = -1;
gz::transport::Node g_node;

void sendFrame(const std::string &hdr, const char *p, size_t n) {
  std::lock_guard<std::mutex> lk(g_tx);
  if (g_fd < 0) return;
  uint32_t len = htonl(static_cast<uint32_t>(n));
  std::string f = hdr + "\n";
  f.append(reinterpret_cast<char *>(&len), 4);
  f.append(p, n);
  size_t off = 0;
  while (off < f.size()) {
    ssize_t w = ::send(g_fd, f.data() + off, f.size() - off, 0);
    if (w <= 0) return;
    off += static_cast<size_t>(w);
  }
}

// gz's RawCallback is void(const char*, size_t, const MessageInfo&).
gz::transport::RawCallback fwd(const std::string &topic,
                                     const std::string &type) {
  return [topic, type](const char *d, const size_t n,
                       const gz::transport::MessageInfo &) {
    sendFrame(R"({"op":"pub","topic":")" + topic + R"(","type":")" + type + R"("})",
              d, n);
  };
}

// Command payloads are protobuf, so they travel base64-encoded inside JSON.
std::string b64d(const std::string &in) {
  static const char *T =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int val = 0, bits = 0;
  std::string out;
  for (char c : in) {
    if (c == '=' || c == '\n') continue;
    const char *p = strchr(T, c);
    if (!p) continue;
    val = (val << 6) | static_cast<int>(p - T);
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((val >> bits) & 0xFF)); }
  }
  return out;
}

std::string fieldOf(const std::string &line, const char *k) {
  std::string key = std::string("\"") + k + "\":\"";
  size_t p = line.find(key);
  if (p == std::string::npos) return "";
  p += key.size();
  return line.substr(p, line.find('"', p) - p);
}

void reader() {
  std::string buf;
  char tmp[65536];
  while (true) {
    ssize_t r = ::recv(g_fd, tmp, sizeof(tmp), 0);
    if (r <= 0) return;
    buf.append(tmp, static_cast<size_t>(r));
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      std::string op = fieldOf(line, "op"), topic = fieldOf(line, "topic");
      std::string type = fieldOf(line, "type"), data = b64d(fieldOf(line, "data"));
      if (op == "sub") {
        g_node.SubscribeRaw(topic, fwd(topic, type), type);
        std::cerr << "gzif: subscribed gz " << topic << "\n";
      } else if (op == "pub") {
        // Container -> gz. Advertise(topic, msgType) yields a Publisher, whose
        // PublishRaw() sends bytes for a type known only at runtime.
        static std::map<std::string, gz::transport::Node::Publisher> pubs;
        auto it = pubs.find(topic);
        if (it == pubs.end()) {
          it = pubs.emplace(topic, g_node.Advertise(topic, type)).first;
          if (!it->second) { std::cerr << "gzif: advertise failed " << topic << "\n"; continue; }
          std::cerr << "gzif: advertising gz " << topic << " [" << type << "]\n";
        }
        it->second.PublishRaw(data, type);
      } else if (op == "call") {
        std::string reply, rt = fieldOf(line, "reptype");
        bool ok = false;
        g_node.RequestRaw(topic, data, type, rt, 2000, reply, ok);
        sendFrame(R"({"op":"reply","topic":")" + topic +
                      R"(","ok":)" + (ok ? "true" : "false") + "}",
                  reply.data(), reply.size());
      }
    }
  }
}
}  // namespace

int main(int argc, char **argv) {
  std::vector<std::string> subs;
  std::string host = "127.0.0.1";
  int port = 9100;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--sub") subs.push_back(argv[++i]);
    else if (a == "--connect") {
      std::string v = argv[++i];
      size_t c = v.rfind(':');
      host = v.substr(0, c);
      port = std::stoi(v.substr(c + 1));
    }
  }

  g_fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, host.c_str(), &a.sin_addr);
  if (::connect(g_fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
    std::cerr << "gzif: connect failed: " << strerror(errno) << "\n";
    return 1;
  }
  int one = 1;
  setsockopt(g_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  std::cerr << "gzif: connected " << host << ":" << port << "\n";

  std::thread(reader).detach();
  for (auto &spec : subs) {
    // spec is "topic" or "topic:gz.msgs.Type"; the manifest declares types
    // because gz-transport13 exposes no topic-type lookup on Node.
    std::string t = spec, type = "gz.msgs.String";
    if (spec.find('.') != std::string::npos) {
      size_t c = spec.rfind(':');
      t = spec.substr(0, c);
      type = spec.substr(c + 1);
    }
    g_node.SubscribeRaw(t, fwd(t, type));
    std::cerr << "gzif: subscribed gz " << t << " [" << type << "]\n";
  }
  while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
