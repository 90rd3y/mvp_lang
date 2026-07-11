import re
with open("src/Interpreter.cpp") as f: c = f.read()
if "case Parser::NodeType::ArrayLiteral:" in c: print("ArrayLiteral is in Interpreter.cpp")
else: print("ArrayLiteral is MISSING")
