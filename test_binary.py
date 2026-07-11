with open("src/Semantic.cpp", "r") as f: c = f.read()
c = c.replace("Lexer::TokenType op = node.token.type;", "Lexer::TokenType op = node.token.type;\n    std::cout << \"DEBUG BINARY OP: \" << (int)op << std::endl;")
with open("src/Semantic.cpp", "w") as f: f.write(c)
