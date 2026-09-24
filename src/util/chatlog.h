#pragma once
// One chat line in the microSD history: how it is written and read back.
//
// Tab-separated, one message per line, so the file opens in any spreadsheet:
//
//   epoch  node  RX|TX  rssi  snr  conv  text
//
//   node   sender NodeNum, 8 hex digits; 00000000 for our own messages
//   conv   conversation: '*' for the group, or the other node's NodeNum in hex
//          for a direct chat (for TX it is who we wrote to, for RX who wrote)
//   text   '\n' is written as the two characters "\n" and '\' as "\\", so a
//          message never spans two lines. Tabs become spaces.
//
// Lines from before direct messages existed have no 'conv' column (six fields
// instead of seven). They still parse, as group messages: the text is the last
// field and never contains a tab, so the field count is not ambiguous.
//
// No Arduino: tested on the host, including the old format.

#include <stddef.h>
#include <stdint.h>

#include "config.h"

namespace util {

struct EntradaChat {
  uint32_t epoch = 0;
  uint32_t nodo  = 0;        // 0 = ours
  uint32_t conv  = 0;        // 0 = group; otherwise the other node
  bool     tx    = false;
  int16_t  rssi  = 0;
  int8_t   snr   = 0;
  char     texto[MSG_BUF_BYTES] = {};
};

// Writes the line WITHOUT the trailing newline. If the text does not fit it is
// cut on a whole UTF-8 character and a whole escape. Returns the length, or 0
// if not even the fixed fields fit.
size_t formatearLineaChat(const EntradaChat& e, char* out, size_t cap);

// Parses one line (a trailing "\r" or "\n" is ignored). false if it is not a
// chat line: a blank line, a truncated write after a power cut, or garbage.
bool analizarLineaChat(const char* linea, EntradaChat* e);

}  // namespace util
