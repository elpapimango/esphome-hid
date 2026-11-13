// PC test for components/hid_streamdeck/jpeg_rotate.h:  test_jpeg_rotate in.jpg out.jpg
// Exit 0 = rotated and written, 2 = the file is not one the rotation handles (the component then serves the original).
// Build: g++ -std=c++17 -O2 -I components/hid_streamdeck -o test_jpeg_rotate tools/test_jpeg_rotate.cpp
#include <cstdio>
#include <vector>

#include "jpeg_rotate.h"

int main(int argc, char **argv) {
  if (argc != 3) return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 1;
  std::vector<uint8_t> in;
  uint8_t buf[4096];
  size_t k;
  while ((k = fread(buf, 1, sizeof buf, f)) > 0) in.insert(in.end(), buf, buf + k);
  fclose(f);
  std::vector<uint8_t> out;
  if (!esphome::hid_streamdeck::jpegrot::rotate180(in.data(), in.size(), out)) return 2;
  f = fopen(argv[2], "wb");
  if (!f) return 1;
  fwrite(out.data(), 1, out.size(), f);
  fclose(f);
  return 0;
}
