import re
with open("src/Interpreter.cpp", "r") as f: content = f.read()

# Fix VarDecl
var_decl = """        case Parser::NodeType::VarDecl: {
            Value val = eval(child_indices[node.children_offset]);
            env.define(node.token.data, val);
            break;
        }"""
old_var_decl = re.compile(r'case Parser::NodeType::VarDecl: \{.*?break;\s*\}', re.DOTALL)
content = old_var_decl.sub(var_decl, content, count=1)

# Fix MemberAccess
member_access = """    case Parser::NodeType::MemberAccess: {
        Value obj_val = eval(child_indices[node.children_offset]);
        uint32_t offset = node.extra_data & 0x7FFFFFFF;
        return obj_val.as.arr[offset];
    }"""
old_member_access = re.compile(r'case Parser::NodeType::MemberAccess: \{.*?return obj_val\.as\.arr\[offset\];\s*\}', re.DOTALL)
content = old_member_access.sub(member_access, content, count=1)

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
content = content.replace("        case Parser::NodeType::NamespaceDecl:", namespace_code)

content = content.replace("functions[node.token.data] = id;", "functions[pool.intern(namespace_prefix + std::string(pool.get(node.token.data)))] = id;")

with open("src/Interpreter.cpp", "w") as f:
    f.write(content)
