#include <algorithm>
#include <cstddef>

extern "C" void stl_like_offset_for_each_transform(int *v, int *out,
                                                   std::size_t n) {
  if (n <= 1)
    return;

  std::for_each(v, v + n, [](int &x) { x += 2; });
  std::transform(v + 1, v + n, v + 1, [](int x) { return x * 2 - 3; });
  std::transform(v, v + n - 1, out, [](int x) { return x + 11; });
}
