#include <algorithm>
#include <cstddef>

extern "C" void stl_like_offset_replace_if(int *v, int *out, std::size_t n) {
  if (n <= 1)
    return;

  std::transform(v, v + n, v, [](int x) { return x + 1; });
  std::replace_if(v + 1, v + n, [](int x) { return x > 100; }, 17);
  std::transform(v, v + n - 1, out, [](int x) { return x * 2 - 1; });
}
