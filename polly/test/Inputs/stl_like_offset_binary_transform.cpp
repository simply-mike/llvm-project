#include <algorithm>
#include <cstddef>

extern "C" void stl_like_offset_binary_transform(int *a, int *b,
                                                 std::size_t n) {
  if (n <= 1)
    return;

  std::transform(a, a + n, a, [](int x) { return x + 1; });
  std::transform(a + 1, a + n, b, a + 1,
                 [](int x, int y) { return x + y * 3; });
  std::transform(a, a + n - 1, b, [](int x) { return x - 5; });
}
