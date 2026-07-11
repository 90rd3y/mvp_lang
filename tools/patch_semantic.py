import re

with open("src/Semantic.cpp", "r") as f:
    content = f.read()

# Add NamespaceDecl to Semantic.cpp
namespace_code = """        case Parser::NodeType::NamespaceDecl: {
            std::string old_prefix = namespace_prefix;
            namespace_prefix += std::string(pool.get(node.token.data)) + "::";
            for (uint32_t i = 0; i < node.children_count; ++i) {
                check(child_indices[node.children_offset + i]);
            }
            namespace_prefix = old_prefix;
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }"""
content = content.replace("        case Parser::NodeType::FuncDecl: {", namespace_code + "\n        case Parser::NodeType::FuncDecl: {")

# In FuncDecl, prefix the function name with namespace_prefix
content = content.replace("functions[node.token.data] = sig;", "functions[pool.intern(namespace_prefix + std::string(pool.get(node.token.data)))] = sig;")

with open("src/Semantic.cpp", "w") as f:
    f.write(content)
