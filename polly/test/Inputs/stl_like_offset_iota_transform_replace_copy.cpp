#include <algorithm>
#include <cstddef>
#include <numeric>

extern "C" void stl_like_offset_iota_transform_replace_copy(int *tmp, int *out,
                                                            std::size_t n,
                                                            int seed) {
  if (n <= 1)
    return;

  std::iota(tmp, tmp + n, seed);
  std::transform(tmp + 1, tmp + n, tmp + 1, [](int x) { return x * 2 + 1; });

  // TODO: offset-aware fusion for STL patterns
  // Prefer a size-stable trailing pass that does not collapse into memcpy so
  // the source-level integration check keeps three loop statements alive.
  std::replace_copy(tmp, tmp + n - 1, out, seed - 1, seed + 7);
}
