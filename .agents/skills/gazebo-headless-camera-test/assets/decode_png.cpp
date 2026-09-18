// decode_png.cpp — native C++ replacement for decode_png.py
//
// Receives camera image frames straight from Gazebo Harmonic transport
// (gz.msgs.Image via gz::transport::Node) instead of scraping the octal-escaped
// text that `gz topic -e` prints. Also keeps a --from-file mode that decodes a
// `gz topic -e` dump so it is a drop-in for the original Python helper.
//
// Usage
//   decode_png -t /camera -o frame.png            # subscribe live (1 frame)
//   decode_png -t /camera -n 120 -w 5 -o f.png    # capture 120 frames (~4 s)
//   decode_png --from-file frame.txt -o f.png     # decode a gz-topic text dump
//
// Diagnostics printed (identical shape to decode_png.py):
//   frame WxH step=S decoded_len=D expected=E
//   luminance min=.. max=.. (dynamic range => real scene)
//   red px=.. (..%)
//   red box rows a..b of H (center H/2) => target in view
//   wrote PNG -> path
//
// Build with build.sh (links gz-transportNN + gz-msgsMM + zlib).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <zlib.h>

#include "gz/msgs/image.pb.h"
#include "gz/transport/Node.hh"

namespace {

// ---- Minimal 8-bit PNG writer (RGB / RGBA) ---------------------------------
void WritePng(const std::string &path, unsigned w, unsigned h, const unsigned char *rgb,
              int bytes_per_px) {
  const bool rgba = (bytes_per_px == 4);

  // Filter byte 0 (None) prepended to every row.
  std::vector<unsigned char> raw(static_cast<size_t>(h) * (1 + w * bytes_per_px));
  for (unsigned y = 0; y < h; ++y) {
    unsigned char *dst = raw.data() + static_cast<size_t>(y) * (1 + w * bytes_per_px);
    dst[0] = 0;
    std::memcpy(dst + 1, rgb + static_cast<size_t>(y) * w * bytes_per_px,
                static_cast<size_t>(w) * bytes_per_px);
  }

  // zlib-compress the scanlines (level 6 to match the Python helper).
  uLongf compBound = compressBound(raw.size());
  std::vector<unsigned char> comp(compBound);
  if (compress2(comp.data(), &compBound, raw.data(), raw.size(), 6) != Z_OK) {
    throw std::runtime_error("zlib compression failed");
  }
  comp.resize(compBound);

  auto chunk = [&](const unsigned char *tag, const unsigned char *data, size_t n,
                   std::string &out) {
    // Big-endian length, tag, data, then CRC32 over tag+data.
    uint32_t len = static_cast<uint32_t>(n);
    out.append(reinterpret_cast<const char *>(&len), 4);
    out.append(reinterpret_cast<const char *>(tag), 4);
    const size_t crcStart = out.size();
    if (n > 0) out.append(reinterpret_cast<const char *>(data), n);
    unsigned long crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef *>(out.data() + crcStart),
                static_cast<uInt>(4 + n));
    out.append(reinterpret_cast<const char *>(&crc), 4);
  };

  std::string png;
  png.append("\x89PNG\r\n\x1a\n", 8);

  unsigned char ihdr[13];
  auto put32 = [](unsigned char *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;  p[3] = v & 0xff;
  };
  put32(ihdr + 0, w);
  put32(ihdr + 4, h);
  ihdr[8] = 8;                  // bit depth
  ihdr[9] = rgba ? 6 : 2;       // color type: RGBA or RGB
  ihdr[10] = 0;                 // compression
  ihdr[11] = 0;                 // filter
  ihdr[12] = 0;                 // interlace
  chunk(reinterpret_cast<const unsigned char *>("IHDR"), ihdr, 13, png);
  chunk(reinterpret_cast<const unsigned char *>("IDAT"), comp.data(), comp.size(), png);
  chunk(reinterpret_cast<const unsigned char *>("IEND"), nullptr, 0, png);

  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path + " for writing");
  f.write(png.data(), png.size());
}

// ---- Shared diagnostics + PNG write ----------------------------------------
void ReportAndWrite(const std::string &out_png, unsigned w, unsigned h, unsigned step,
                    const std::vector<unsigned char> &px, int bytes_per_px) {
  std::cout << "frame " << w << "x" << h << " step=" << step
            << " decoded_len=" << px.size() << " expected="
            << (static_cast<size_t>(w) * h * bytes_per_px) << "\n";

  const long total = static_cast<long>(w) * h;
  long minv = 255, maxv = 0, red = 0;
  std::vector<int> redRows(h, 0);
  for (long y = 0; y < h; ++y) {
    for (long x = 0; x < w; ++x) {
      const unsigned char *p =
          px.data() + (static_cast<size_t>(y) * w + x) * bytes_per_px;
      long r = p[0], g = p[1], b = p[2];
      long lum = (r + g + b) / 3;
      if (lum < minv) minv = lum;
      if (lum > maxv) maxv = lum;
      if (r > 120 && g < 90 && b < 90) {
        ++red;
        ++redRows[y];
      }
    }
  }
  std::cout << "luminance min=" << minv << " max=" << maxv
            << " (dynamic range => real scene)\n";
  std::cout << "red px=" << red << " (" << (100.0 * red / total) << "%)\n";

  long lo = -1, hi = -1;
  for (long y = 0; y < h; ++y) {
    if (redRows[y] > 0) {
      if (lo < 0) lo = y;
      hi = y;
    }
  }
  if (lo >= 0) {
    std::cout << "red box rows " << lo << ".." << hi << " of " << h
              << " (center " << (h / 2) << ") => target in view\n";
  }
  WritePng(out_png, w, h, px.data(), bytes_per_px);
  std::cout << "wrote PNG -> " << out_png << "\n";
}

// ---- Octal-escape decode (matches decode_png.py) ---------------------------
std::vector<unsigned char> UnescapeOctal(const std::string &txt) {
  std::vector<unsigned char> out;
  out.reserve(txt.size());
  auto isOctal = [](char c) { return c >= '0' && c <= '7'; };
  size_t i = 0;
  while (i < txt.size()) {
    char c = txt[i];
    if (c == '\\' && i + 1 < txt.size() && isOctal(txt[i + 1])) {
      size_t j = i + 1;
      std::string o;
      while (j < txt.size() && o.size() < 3 && isOctal(txt[j])) {
        o += txt[j];
        ++j;
      }
      out.push_back(static_cast<unsigned char>(std::stoi(o, nullptr, 8)));
      i = j;
      continue;
    }
    out.push_back(static_cast<unsigned char>(c));
    ++i;
  }
  return out;
}

// Extract the binary payload between `data: "` and the closing quote, plus the
// header fields (width/height/step) from a `gz topic -e` text dump.
struct FrameFromFile {
  unsigned w = 0, h = 0, step = 0;
  std::vector<unsigned char> data;
  int fmt = 3; // RGB_INT8 default
};

bool ParseTopicFile(const std::string &path, FrameFromFile &fr) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::stringstream ss;
  ss << f.rdbuf();
  std::string txt = ss.str();

  std::smatch m;
  if (!std::regex_search(txt, m, std::regex("width: (\\d+)\\nheight: (\\d+)\\nstep: (\\d+)")))
    throw std::runtime_error("could not parse width/height/step from " + path);
  fr.w = std::stoi(m[1]);
  fr.h = std::stoi(m[2]);
  fr.step = std::stoi(m[3]);

  size_t dstart = txt.find("data: \"");
  if (dstart == std::string::npos) throw std::runtime_error("no data field in " + path);
  dstart += std::strlen("data: \"");
  size_t dend = txt.find('"', dstart);
  if (dend == std::string::npos) dend = txt.size();
  fr.data = UnescapeOctal(txt.substr(dstart, dend - dstart));

  // pixel format token, e.g. `pixel_format_type: RGB_INT8`
  size_t pf = txt.find("pixel_format_type:");
  if (pf != std::string::npos) {
    std::string tok = txt.substr(pf);
    if (tok.find("RGBA") != std::string::npos) fr.fmt = 4;
    else if (tok.find("RGB") != std::string::npos) fr.fmt = 3;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  std::string topic, out_png = "frame.png";
  std::string from_file;
  int count = 1;
  int wait_secs = 20;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
      return argv[++i];
    };
    if (a == "-t" || a == "--topic") topic = next();
    else if (a == "-n" || a == "--count") count = std::stoi(next());
    else if (a == "-w" || a == "--wait") wait_secs = std::stoi(next());
    else if (a == "-o" || a == "--output") out_png = next();
    else if (a == "--from-file") from_file = next();
    else if (a == "-h" || a == "--help") {
      std::cout <<
          "decode_png — capture/decode a Gazebo camera frame into a PNG\n"
          "  -t, --topic TOPIC     subscribe live to TOPIC (e.g. /camera)\n"
          "  -n, --count N         frames to capture (default 1)\n"
          "  -w, --wait SECS       max seconds to wait for first frame (default 20)\n"
          "  -o, --output FILE     output PNG path (default frame.png)\n"
          "      --from-file FILE  decode a `gz topic -e` text dump (drop-in for decode_png.py)\n"
          "  -h, --help            this help\n";
      return 0;
    } else {
      throw std::runtime_error("unknown arg: " + a);
    }
  }

  try {
    if (!from_file.empty()) {
      // Drop-in mode: decode a `gz topic -e` text dump.
      FrameFromFile fr;
      ParseTopicFile(from_file, fr);
      const int bpp = (fr.fmt == 4) ? 4 : 3;
      ReportAndWrite(out_png, fr.w, fr.h, fr.step, fr.data, bpp);
      return 0;
    }

    if (topic.empty()) {
      std::cerr << "error: provide -t/--topic TOPIC or --from-file FILE\n";
      return 2;
    }

    // Native mode: subscribe with gz::transport and receive gz::msgs::Image.
    gz::transport::Node node;

    std::atomic<int> received{0};
    std::atomic<bool> stop{false};
    FrameFromFile last;
    std::mutex mtx;

    bool ok = node.Subscribe<gz::msgs::Image>(topic, [&](const gz::msgs::Image &msg) {
      std::lock_guard<std::mutex> lk(mtx);
      last.w = msg.width();
      last.h = msg.height();
      last.step = msg.step();
      last.fmt = static_cast<int>(msg.pixel_format_type());
      last.data.assign(msg.data().begin(), msg.data().end());
      received.fetch_add(1);
      if (received.load() >= count) stop.store(true);
    });
    if (!ok) {
      std::cerr << "failed to subscribe to " << topic << "\n";
      return 1;
    }

    auto t0 = std::chrono::steady_clock::now();
    while (received.load() < count) {
      if (stop.load()) break;
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::seconds>(now - t0).count() > wait_secs)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (received.load() == 0) {
      std::cerr << "error: no frames on " << topic << " within " << wait_secs
                << " s (is the sim running with the Sensors plugin?)\n";
      return 1;
    }

    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
    int got = received.load();
    if (count > 1) {
      std::cout << "captured " << got << " frames in " << secs << " s (~"
                << (got / secs) << " fps)\n";
    }

    const int bpp = (last.fmt == 4) ? 4 : 3;
    ReportAndWrite(out_png, last.w, last.h, last.step, last.data, bpp);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
