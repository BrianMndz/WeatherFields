// expect-error: no type named 'byte' in namespace 'std'
// std::byte arrived in libstdc++ 7; the board's Debian 9 has libstdc++ 6.
#include <cstddef>

std::byte value{};
