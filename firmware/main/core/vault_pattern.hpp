#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace vault::pattern {
// Row-major cells 0..8, encoded as '1'..'9' only inside the auth transport.
// Standard 3x3 rules: no reused cell, insert an unvisited midpoint, >=4 cells.
constexpr int Side = 240, First = 48, Pitch = 72, HitRadius = 22;
constexpr size_t Minimum = 4, Maximum = 9;
constexpr int x(unsigned cell) { return First + int(cell % 3) * Pitch; }
constexpr int y(unsigned cell) { return First + int(cell / 3) * Pitch; }
constexpr int midpoint(unsigned from, unsigned to) {
  const int ax = from % 3, ay = from / 3, bx = to % 3, by = to / 3;
  return from != to && (ax + bx) % 2 == 0 && (ay + by) % 2 == 0
             ? ((ay + by) / 2) * 3 + (ax + bx) / 2 : -1;
}
constexpr bool valid(const char *encoded, size_t count) {
  if (!encoded || count < Minimum || count > Maximum) return false;
  unsigned used = 0;
  for (size_t i = 0; i < count; ++i) {
    if (encoded[i] < '1' || encoded[i] > '9') return false;
    const unsigned cell = encoded[i] - '1';
    if (used & (1u << cell)) return false;
    if (i) {
      const int middle = midpoint(encoded[i - 1] - '1', cell);
      if (middle >= 0 && !(used & (1u << middle))) return false;
    }
    used |= 1u << cell;
  }
  return true;
}
class Stroke {
public:
  constexpr void clear() { cells = {}; count = 0; used = 0; down = false; px = py = 0; }
  constexpr void add(unsigned cell) {
    if (cell >= Maximum || (used & (1u << cell))) return;
    if (count) {
      const int middle = midpoint(cells[count - 1], cell);
      if (middle >= 0 && !(used & (1u << middle))) append(unsigned(middle));
    }
    append(cell);
  }
  constexpr void begin(int tx, int ty) {
    clear(); down = true; px = bound(tx); py = bound(ty); move(tx, ty);
  }
  constexpr void move(int tx, int ty) {
    if (!down) return;
    tx = bound(tx); ty = bound(ty);
    const int dx = tx - px, dy = ty - py, length = dx * dx + dy * dy;
    struct Hit { int64_t position; unsigned cell; };
    std::array<Hit, Maximum> hits{};
    size_t size = 0;
    // Segment hit-testing fills cells crossed between two touch samples.
    for (unsigned cell = 0; cell < Maximum; ++cell) {
      if (used & (1u << cell)) continue;
      const int cx = x(cell) - px, cy = y(cell) - py;
      int64_t dot = int64_t(cx) * dx + int64_t(cy) * dy;
      bool hit = false;
      if (length == 0 || dot <= 0) { hit = cx * cx + cy * cy <= HitRadius * HitRadius; dot = 0; }
      else if (dot >= length) { const int ex=x(cell)-tx, ey=y(cell)-ty; hit=ex*ex+ey*ey<=HitRadius*HitRadius; dot=length; }
      else { const int64_t cross=int64_t(cx)*dy-int64_t(cy)*dx; hit=cross*cross<=int64_t(HitRadius)*HitRadius*length; }
      if (!hit) continue;
      size_t index = size++;
      while (index && hits[index - 1].position > dot) { hits[index] = hits[index - 1]; --index; }
      hits[index] = {dot, cell};
    }
    for (size_t i = 0; i < size; ++i) add(hits[i].cell);
    px = tx; py = ty;
  }
  constexpr void end(int tx, int ty) { move(tx, ty); down = false; }
  constexpr bool acceptable() const { return count >= Minimum; }
  constexpr bool selected(unsigned cell) const { return used & (1u << cell); }
  std::array<uint8_t, Maximum> cells{};
  size_t count = 0;
  bool down = false;
  int px = 0, py = 0;
private:
  unsigned used = 0;
  static constexpr int bound(int value) { return value < 0 ? 0 : value >= Side ? Side - 1 : value; }
  constexpr void append(unsigned cell) { if (count < Maximum) { cells[count++] = uint8_t(cell); used |= 1u << cell; } }
};
} // namespace vault::pattern
