// expect-error: expected unqualified-id
// Structured bindings need Clang 4 or GCC 7; the board has Clang 3.9.
#include <utility>

int sum()
{
    auto [a, b] = std::pair<int, int>{1, 2};
    return a + b;
}
