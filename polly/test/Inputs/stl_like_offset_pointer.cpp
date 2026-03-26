#include <algorithm>
#include <cstddef>

extern "C" void stl_like_offset_pointer(int *v, int *out, std::size_t n) {
  if (n <= 1)
    return;

  std::fill(v, v + n, 0);
  std::transform(v + 1, v + n, v + 1, [](int x) { return x * 2 + 1; });

  // TODO: offset-aware fusion for STL patterns
  // Keep the trailing algorithm affine and size-stable so the source-level
  // integration check stays focused on constant-offset fusion instead of
  // output-sensitive compaction lowering.
  std::transform(v, v + n - 1, out, [](int x) { return x + 3; });
}
