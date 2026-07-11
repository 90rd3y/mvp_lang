import sys

def patch():
    with open("src/Semantic.cpp", "r") as f:
        content = f.read()

    # 1. Replace "семантическая ошибка" with "статическая ошибка"
    content = content.replace("семантическая ошибка", "статическая ошибка")

    # 2. Extract and replace check_binary
    start_str = "TypeId Analyzer::check_binary(Parser::NodeId id) {"
    start_idx = content.find(start_str)
    if start_idx == -1:
        print("Could not find check_binary start")
        sys.exit(1)
        
    end_str = "}\n\n\n\nvoid Analyzer::analyze"
    end_idx = content.find(end_str, start_idx)
    if end_idx == -1:
        print("Could not find check_binary end")
        sys.exit(1)

    old_func = content[start_idx:end_idx + 1]

    new_func = """TypeId Analyzer::check_binary(Parser::NodeId id) {
    const auto& node = nodes[id];
    TypeId left = check(child_indices[node.children_offset]);
    TypeId right = check(child_indices[node.children_offset + 1]);

    auto resolve_alias = [&](TypeId id) {
        while (type_table.get_type(id).kind == TypeKind::Alias) {
            id = type_table.get_type(id).base_type;
        }
        return id;
    };

    if (resolve_alias(left) != resolve_alias(right)) {
        error(node.token, "Несоответствие типов в бинарной операции");
    }

    Lexer::TokenType op = node.token.type;
    
    // Проверка операторов отношения (<, >, <=, >=)
    if (op == Lexer::TokenType::Less || op == Lexer::TokenType::LessEqual ||
        op == Lexer::TokenType::Greater || op == Lexer::TokenType::GreaterEqual) {
        
        Semantic::TypeKind k = type_table.get_type(resolve_alias(left)).kind;
        if (k != TypeKind::Int && k != TypeKind::Float && k != TypeKind::Char) {
            error(node.token, "Операторы отношения (<, >, <=, >=) применимы только к числам и символам");
        }
        return type_table.get_builtin(TypeKind::Bool);
    }

    // Проверка операторов равенства (==, !=)
    if (op == Lexer::TokenType::EqualEqual || op == Lexer::TokenType::BangEqual) {
        return type_table.get_builtin(TypeKind::Bool);
    }

    // Для арифметики возвращаем тип операндов
    return left;
}"""

    content = content[:start_idx] + new_func + content[end_idx + 1:]

    with open("src/Semantic.cpp", "w") as f:
        f.write(content)

    print("Patched successfully")

if __name__ == "__main__":
    patch()
