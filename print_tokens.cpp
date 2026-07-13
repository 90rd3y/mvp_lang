#include "inc/Lexer.hpp"
#include <iostream>
#include <fstream>
int main() {
    std::ifstream file("test.lang");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Memory::Arena arena;
    Lexer::StringPool pool(arena);
    Lexer::Scanner scanner(source, pool);
    auto tokens = scanner.tokenize();
    for(auto t : tokens) {
        std::cout << "Token type: " << (int)t.type << " lexeme: " << t.lexeme << " line: " << t.line << " col: " << t.column << std::endl;
    }
}
