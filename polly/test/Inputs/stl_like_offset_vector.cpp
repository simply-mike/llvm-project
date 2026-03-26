#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

extern "C" std::size_t stl_like_offset_vector(std::size_t n, int seed) {
  std::vector<int> v(n, seed);
  std::vector<int> result;
  result.reserve(n);

  std::fill(v.begin(), v.end(), seed + 1);

  if (n > 1) {
    std::transform(v.begin() + 1, v.end(), v.begin() + 1,
                   [](int x) { return x * 2 + 1; });
  }

  auto copy_end = n == 0 ? v.begin() : v.end() - 1;
  std::copy_if(v.begin(), copy_end, std::back_inserter(result),
               [](int x) { return x > 10; });
  return result.size();
}
