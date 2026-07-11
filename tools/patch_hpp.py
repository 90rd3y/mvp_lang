import re
with open("inc/Interpreter.hpp", "r") as f: content = f.read()
content = content.replace("std::unordered_map<Lexer::IdentId, Parser::NodeId> functions;", "std::unordered_map<Lexer::IdentId, Parser::NodeId> functions;\n  std::string namespace_prefix = \"\";")
with open("inc/Interpreter.hpp", "w") as f: f.write(content)
