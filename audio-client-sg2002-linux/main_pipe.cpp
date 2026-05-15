#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include "wakeword.h"
}

int main(int argc, char **argv) {
  float threshold = 0.995f;
  int chunk_size = 160;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--threshold" && i + 1 < argc)
      threshold = std::strtof(argv[++i], nullptr);
    else if (arg == "--chunk" && i + 1 < argc) {
      chunk_size = std::atoi(argv[++i]);
      if (chunk_size <= 0) chunk_size = 160;
    }
  }

  if (wakeword_init(threshold) != 0) {
    std::cerr << "wakeword_init failed\n";
    return 1;
  }

  std::vector<int16_t> buf(chunk_size);
  std::cerr << "Listening... say 'loopy'\n";
  std::cerr.flush();

  while (true) {
    size_t got = 0;
    while (got < static_cast<size_t>(chunk_size)) {
      size_t n = fread(buf.data() + got, sizeof(int16_t),
                       chunk_size - static_cast<int>(got), stdin);
      if (n == 0) goto done;
      got += n;
    }
    if (wakeword_feed(buf.data(), chunk_size)) {
      std::cout << "*** WAKEWORD DETECTED ***\n";
      std::cout.flush();
      wakeword_reset();
    }
  }
done:
  return 0;
}
