// Panel files in flash: LittleFS on the partition named "spiffs", mounted at /fs.
#include "panel_storage.h"

#include <LittleFS.h>

namespace storage {

bool begin() { return LittleFS.begin(/*format if it won't mount*/ true, "/fs", /*open files*/ 5, "spiffs"); }

bool read(const char *path, std::string &out) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out.resize(f.size());
  const bool whole = f.read((uint8_t *)&out[0], out.size()) == out.size();
  f.close();
  return whole;
}

// Written to "<path>.tmp" first and then renamed, so a power cut mid-write never
// leaves a half-written file behind.
bool write(const char *path, const std::string &data) {
  const std::string tmp = std::string(path) + ".tmp";
  File f = LittleFS.open(tmp.c_str(), "w");
  if (!f) return false;
  const bool whole = f.write((const uint8_t *)data.data(), data.size()) == data.size();
  f.close();
  if (!whole) return false;
  LittleFS.remove(path);
  return LittleFS.rename(tmp.c_str(), path);
}

}  // namespace storage
