import re

with open("src/Interpreter.cpp", "r") as f:
    content = f.read()

# Add NamespaceDecl
namespace_code = """        case Parser::NodeType::NamespaceDecl: {
            std::string old_prefix = namespace_prefix;
            namespace_prefix += std::string(pool.get(node.token.data)) + "::";
            for (uint32_t i = 0; i < node.children_count; ++i) {
                execute(child_indices[node.children_offset + i]);
            }
            namespace_prefix = old_prefix;
            break;
        }"""
content = content.replace("        case Parser::NodeType::FuncDecl: {", namespace_code + "\n        case Parser::NodeType::FuncDecl: {")

# In FuncDecl, prefix function name
content = content.replace("functions[node.token.data] = id;", "functions[pool.intern(namespace_prefix + std::string(pool.get(node.token.data)))] = id;")

with open("src/Interpreter.cpp", "w") as f:
    f.write(content)
