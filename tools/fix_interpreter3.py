with open("src/Interpreter.cpp", "r") as f: content = f.read()

# Fix VarDecl
import re
var_decl = """        case Parser::NodeType::VarDecl: {
            Value val = eval(child_indices[node.children_offset]);
            env.define(node.token.data, val);
            break;
        }"""
content = re.sub(r'case Parser::NodeType::VarDecl: \{.*?break;\s*\}', var_decl, content, flags=re.DOTALL)

# Fix MemberAccess
member_access = """    case Parser::NodeType::MemberAccess: {
        Value obj_val = eval(child_indices[node.children_offset]);
        uint32_t offset = node.extra_data;
        return obj_val.as.arr[offset];
    }"""
content = re.sub(r'case Parser::NodeType::MemberAccess: \{.*?return obj_val\.as\.arr\[offset\];\s*\}', member_access, content, flags=re.DOTALL)

# Add NamespaceDecl logic
namespace_code = """        case Parser::NodeType::NamespaceDecl: {
            std::string old_prefix = namespace_prefix;
            namespace_prefix += std::string(pool.get(node.token.data)) + "::";
            for (uint32_t i = 0; i < node.children_count; ++i) {
                execute(child_indices[node.children_offset + i]);
            }
            namespace_prefix = old_prefix;
            break;
        }"""
if "case Parser::NodeType::NamespaceDecl" not in content:
    content = content.replace("case Parser::NodeType::FuncDecl: {", namespace_code + "\n        case Parser::NodeType::FuncDecl: {")

# Function prefixing
content = content.replace("functions[node.token.data] = id;", "functions[pool.intern(namespace_prefix + std::string(pool.get(node.token.data)))] = id;")

# Fix remaining compile errors:
content = content.replace("case Parser::NodeType::Assign:", "case Parser::NodeType::AssignStmt:")
content = re.sub(r'case Parser::NodeType::BuiltinInput:.*?return v;\s*\}', '', content, flags=re.DOTALL)
content = re.sub(r'case Parser::NodeType::BuiltinExit:.*?case Parser::NodeType::Call: \{', 'case Parser::NodeType::Call: {', content, flags=re.DOTALL)
content = re.sub(r'if \(node\.token\.type == Lexer::TokenType::KwPrint\).*?\}\s*else', '', content, flags=re.DOTALL)

with open("src/Interpreter.cpp", "w") as f: f.write(content)
