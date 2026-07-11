import re

with open("src/Interpreter.cpp", "r") as f: content = f.read()

# 1. VarDecl
var_decl = """        case Parser::NodeType::VarDecl: {
            Value val = eval(child_indices[node.children_offset]);
            env.define(node.token.data, val);
            break;
        }"""
content = re.sub(r'case Parser::NodeType::VarDecl: \{.*?break;\s*\}', var_decl, content, flags=re.DOTALL)

# 2. MemberAccess
member_access = """    case Parser::NodeType::MemberAccess: {
        Value obj_val = eval(child_indices[node.children_offset]);
        uint32_t offset = node.extra_data;
        return obj_val.as.arr[offset];
    }"""
content = re.sub(r'case Parser::NodeType::MemberAccess: \{.*?return obj_val\.as\.arr\[offset\];\s*\}', member_access, content, flags=re.DOTALL)

# 3. NamespaceDecl
namespace_code = """        case Parser::NodeType::NamespaceDecl: {
            std::string old_prefix = namespace_prefix;
            namespace_prefix += std::string(pool.get(node.token.data)) + "::";
            for (uint32_t i = 0; i < node.children_count; ++i) {
                execute(child_indices[node.children_offset + i]);
            }
            namespace_prefix = old_prefix;
            break;
        }"""
# Only add if not present
if "case Parser::NodeType::NamespaceDecl" not in content:
    content = content.replace("case Parser::NodeType::FuncDecl: {", namespace_code + "\n        case Parser::NodeType::FuncDecl: {")

# 4. Function prefixing
content = content.replace("functions[node.token.data] = id;", "functions[pool.intern(namespace_prefix + std::string(pool.get(node.token.data)))] = id;")

with open("src/Interpreter.cpp", "w") as f: f.write(content)
