#include <algorithm>
#include <cstddef>

extern "C" void stl_like_offset_fill_transform_replace_copy(int *v, int *out,
                                                            std::size_t n) {
  if (n <= 1)
    return;

  std::fill(v, v + n, 5);
  std::transform(v + 1, v + n, v + 1, [](int x) { return x * 3 + 1; });
  std::replace_copy(v, v + n - 1, out, 6, 19);
}
