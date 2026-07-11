#include <iostream>
#include "Lexer.hpp"
int main() {
    Lexer::StringPool pool;
    Lexer::IdentId id1 = pool.intern("main");
    Lexer::IdentId id2 = pool.intern("main");
    std::cout << "id1=" << id1 << " id2=" << id2 << std::endl;
}
