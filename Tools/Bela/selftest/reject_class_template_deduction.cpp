// expect-error: requires template arguments
// Class template argument deduction needs Clang 5; the board has Clang 3.9.
#include <utility>

std::pair p{1, 2.0};
