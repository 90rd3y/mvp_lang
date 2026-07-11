#include "Parser.hpp"
#include "Lexer.hpp"
#include "Memory.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

using namespace Parser;

void print_ast(Parser::Parser& p, NodeId id, int depth) {
    if (id == InvalidNode) return;
    const auto& n = p.get_node(id);
    for(int i=0; i<depth; i++) std::cout << "  ";
    std::cout << "Node type: " << (int)n.type << " lexeme: " << n.token.lexeme << "\n";
    for(uint32_t i=0; i<n.children_count; ++i) {
        print_ast(p, p.get_child(id, i), depth+1);
    }
}

int main(int argc, char** argv) {
    std::ifstream file(argv[1]);
    std::stringstream buffer;
    buffer << file.rdbuf();
    Lexer::StringPool pool;
    Lexer::Lexer lexer(buffer.str(), pool);
    auto tokens = lexer.tokenize();
    Parser::Parser parser(tokens);
    NodeId root = parser.parse();
    print_ast(parser, root, 0);
    return 0;
}
