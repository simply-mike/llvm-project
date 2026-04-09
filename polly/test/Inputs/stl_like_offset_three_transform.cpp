#include <algorithm>
#include <cstddef>

extern "C" void stl_like_offset_three_transform(int *v, int *out,
                                                std::size_t n) {
  if (n <= 1)
    return;

  // Keep all three passes as loop statements so the source-level integration
  // check exercises 3-way constant-offset fusion directly.
  std::transform(v, v + n, v, [](int x) { return x + 1; });
  std::transform(v + 1, v + n, v + 1, [](int x) { return x * 2 + 1; });
  std::transform(v, v + n - 1, out, [](int x) { return x + 3; });
}
