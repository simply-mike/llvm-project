#include <algorithm>
#include <cstddef>

extern "C" void stl_like_offset_replace_copy_if(int *v, int *out,
                                                std::size_t n) {
  if (n <= 1)
    return;

  std::transform(v, v + n, v, [](int x) { return x + 4; });
  std::replace_copy_if(v + 1, v + n, out, [](int x) { return x < 0; }, 23);
  std::transform(out, out + n - 1, out, [](int x) { return x * 2 + 1; });
}
