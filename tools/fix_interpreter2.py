import re
with open("src/Interpreter.cpp", "r") as f: content = f.read()

content = content.replace("case Parser::NodeType::Assign:", "case Parser::NodeType::AssignStmt:")

builtin_eval_pattern = re.compile(r'case Parser::NodeType::BuiltinInput:.*?return v;\s*\}', re.DOTALL)
content = builtin_eval_pattern.sub('', content)

builtin_exec_pattern = re.compile(r'case Parser::NodeType::BuiltinExit:.*?case Parser::NodeType::Call: \{', re.DOTALL)
content = builtin_exec_pattern.sub('case Parser::NodeType::Call: {', content)

# Remove the BuiltinPrint block which uses KwPrint (which doesn't exist)
builtin_print_pattern = re.compile(r'if \(node\.token\.type == Lexer::TokenType::KwPrint\).*?\}\s*else', re.DOTALL)
content = builtin_print_pattern.sub('', content)

with open("src/Interpreter.cpp", "w") as f: f.write(content)
