#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

extern "C" {
#include "wakeword.h"
}

namespace {

struct WavData {
  uint32_t sample_rate = 0;
  uint16_t channels = 0;
  uint16_t bits_per_sample = 0;
  std::vector<int16_t> samples;
};

uint16_t ReadU16(const unsigned char *ptr) {
  return static_cast<uint16_t>(ptr[0] | (ptr[1] << 8));
}

uint32_t ReadU32(const unsigned char *ptr) {
  return static_cast<uint32_t>(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) |
                               (ptr[3] << 24));
}

bool LoadWav(const std::string &path, WavData *out) {
  if (!out)
    return false;

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open WAV: " << path << '\n';
    return false;
  }

  unsigned char riff_header[12] = {};
  file.read(reinterpret_cast<char *>(riff_header), sizeof(riff_header));
  if (file.gcount() != static_cast<std::streamsize>(sizeof(riff_header)) ||
      std::string(reinterpret_cast<char *>(riff_header), 4) != "RIFF" ||
      std::string(reinterpret_cast<char *>(riff_header + 8), 4) != "WAVE") {
    std::cerr << "Not a RIFF/WAVE file: " << path << '\n';
    return false;
  }

  bool have_fmt = false;
  bool have_data = false;
  std::vector<unsigned char> data_bytes;

  while (file && (!have_fmt || !have_data)) {
    unsigned char chunk_header[8] = {};
    file.read(reinterpret_cast<char *>(chunk_header), sizeof(chunk_header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(chunk_header)))
      break;

    const std::string chunk_id(reinterpret_cast<char *>(chunk_header), 4);
    const uint32_t chunk_size = ReadU32(chunk_header + 4);

    if (chunk_id == "fmt ") {
      std::vector<unsigned char> fmt(chunk_size);
      file.read(reinterpret_cast<char *>(fmt.data()), chunk_size);
      if (file.gcount() != static_cast<std::streamsize>(chunk_size) ||
          chunk_size < 16) {
        std::cerr << "Invalid fmt chunk in " << path << '\n';
        return false;
      }

      const uint16_t audio_format = ReadU16(fmt.data());
      out->channels = ReadU16(fmt.data() + 2);
      out->sample_rate = ReadU32(fmt.data() + 4);
      out->bits_per_sample = ReadU16(fmt.data() + 14);
      if (audio_format != 1) {
        std::cerr << "Unsupported WAV format (need PCM): " << path << '\n';
        return false;
      }
      have_fmt = true;
    } else if (chunk_id == "data") {
      data_bytes.resize(chunk_size);
      file.read(reinterpret_cast<char *>(data_bytes.data()), chunk_size);
      if (file.gcount() != static_cast<std::streamsize>(chunk_size)) {
        std::cerr << "Unexpected EOF in data chunk: " << path << '\n';
        return false;
      }
      have_data = true;
    } else {
      file.seekg(chunk_size, std::ios::cur);
    }

    if ((chunk_size & 1U) != 0U)
      file.seekg(1, std::ios::cur);
  }

  if (!have_fmt || !have_data) {
    std::cerr << "Missing fmt/data chunk in WAV: " << path << '\n';
    return false;
  }
  if (out->sample_rate != 16000) {
    std::cerr << "Unsupported sample rate (need 16000 Hz): " << path << '\n';
    return false;
  }
  if (out->bits_per_sample != 16) {
    std::cerr << "Unsupported bit depth (need 16-bit PCM): " << path << '\n';
    return false;
  }
  if (out->channels == 0) {
    std::cerr << "Invalid channel count in WAV: " << path << '\n';
    return false;
  }

  const size_t sample_count = data_bytes.size() / sizeof(int16_t);
  std::vector<int16_t> interleaved(sample_count);
  std::memcpy(interleaved.data(), data_bytes.data(), sample_count * sizeof(int16_t));

  if (out->channels == 1) {
    out->samples.swap(interleaved);
    return true;
  }

  const size_t frames = interleaved.size() / out->channels;
  out->samples.resize(frames);
  for (size_t frame = 0; frame < frames; ++frame) {
    int32_t mixed = 0;
    for (uint16_t ch = 0; ch < out->channels; ++ch) {
      mixed += interleaved[frame * out->channels + ch];
    }
    mixed /= static_cast<int32_t>(out->channels);
    mixed = std::max<int32_t>(-32768, std::min<int32_t>(32767, mixed));
    out->samples[frame] = static_cast<int16_t>(mixed);
  }
  return true;
}

void PrintUsage(const char *argv0) {
  std::cerr << "Usage: " << argv0 << " --wav input.wav [--threshold 0.995]\n"
            << "\n"
            << "The input file must be 16 kHz PCM 16-bit mono or stereo.\n"
            << "This build is ready for SG2002 Linux before INMP441 is wired.\n";
}

}  // namespace

int main(int argc, char **argv) {
  std::string wav_path;
  float threshold = 0.995f;
  int chunk_size = 160;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--wav" && i + 1 < argc) {
      wav_path = argv[++i];
    } else if (arg == "--threshold" && i + 1 < argc) {
      threshold = std::strtof(argv[++i], nullptr);
    } else if (arg == "--chunk" && i + 1 < argc) {
      chunk_size = std::atoi(argv[++i]);
      if (chunk_size <= 0)
        chunk_size = 160;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (wav_path.empty()) {
    PrintUsage(argv[0]);
    return 1;
  }

  WavData wav;
  if (!LoadWav(wav_path, &wav))
    return 1;

  if (wakeword_init(threshold) != 0) {
    std::cerr << "wakeword_init failed\n";
    return 1;
  }

  std::cout << "Loaded " << wav_path << " (" << wav.samples.size()
            << " samples, " << wav.sample_rate << " Hz)\n";

  bool fired = false;
  for (size_t offset = 0; offset < wav.samples.size(); offset += chunk_size) {
    const int remaining = static_cast<int>(wav.samples.size() - offset);
    const int n_samples = std::min(chunk_size, remaining);
    if (n_samples <= 0)
      break;

    if (wakeword_feed(wav.samples.data() + offset, n_samples)) {
      std::cout << "Wakeword detected at sample " << offset << '\n';
      fired = true;
      break;
    }
  }

  wakeword_reset();
  return fired ? 0 : 2;
}
