import re

with open("src/Semantic.cpp", "r") as f:
    content = f.read()

# 1. Rename Assign -> AssignStmt
content = content.replace("case Parser::NodeType::Assign:", "case Parser::NodeType::AssignStmt:")

# 2. Remove Builtin AST nodes handling (Lines containing BuiltinExit to BuiltinInput)
builtin_pattern = re.compile(r'case Parser::NodeType::BuiltinExit:.*?case Parser::NodeType::BuiltinInput: {\s*result = type_table\.get_builtin\(TypeKind::String\);\s*break;\s*}', re.DOTALL)
content = builtin_pattern.sub('', content)

# 3. Add resolve_alias and types_compatible helper functions at the beginning
helpers = """
TypeId Analyzer::resolve_alias(TypeId id) {
    while (type_table.get_type(id).kind == TypeKind::Alias) {
        id = type_table.get_type(id).base_type;
    }
    return id;
}

bool Analyzer::types_compatible(TypeId t1, TypeId t2) {
    TypeId r1 = resolve_alias(t1);
    TypeId r2 = resolve_alias(t2);
    if (r1 == r2) return true;
    auto info1 = type_table.get_type(r1);
    auto info2 = type_table.get_type(r2);
    if (info1.kind == TypeKind::Array && info2.kind == TypeKind::Array) {
        return info1.size == info2.size && resolve_alias(info1.base_type) == resolve_alias(info2.base_type);
    }
    return false;
}
"""
content = content.replace("TypeId Analyzer::check(Parser::NodeId id) {", helpers + "\nTypeId Analyzer::check(Parser::NodeId id) {")

# 4. In VarDecl, remove local resolve_alias
var_decl_resolve_alias = """        auto resolve_alias = [&](TypeId id) {
            while (type_table.get_type(id).kind == TypeKind::Alias) {
                id = type_table.get_type(id).base_type;
            }
            return id;
        };"""
content = content.replace(var_decl_resolve_alias, "")

# 5. In VarDecl, change strict type checking to types_compatible
content = content.replace('if (resolve_alias(init_type) != resolve_alias(final_type)) error(node.token, "Тип инициализатора не совпадает с типом переменной");', 'if (!types_compatible(init_type, final_type)) error(node.token, "Тип инициализатора не совпадает с типом переменной");')

# 6. In Indexing, remove local resolve_alias and update check
idx_resolve_alias = """        auto resolve_alias = [&](TypeId id) {
            while (type_table.get_type(id).kind == TypeKind::Alias) {
                id = type_table.get_type(id).base_type;
            }
            return id;
        };
        
        if (resolve_alias(idx_type) != type_table.get_builtin(TypeKind::Int)) error(node.token, "Индекс должен быть целым числом");"""
idx_replacement = '        if (!types_compatible(idx_type, type_table.get_builtin(TypeKind::Int))) error(node.token, "Индекс должен быть целым числом");'
content = content.replace(idx_resolve_alias, idx_replacement)

# 7. Add ArrayLiteral and StructLiteral logic before ExprStmt
literals_code = """        case Parser::NodeType::ArrayLiteral: {
            if (node.children_count == 0) error(node.token, "Пустые массивы не поддерживаются");
            TypeId base_type = check(child_indices[node.children_offset]);
            for (uint32_t i = 1; i < node.children_count; ++i) {
                if (!types_compatible(check(child_indices[node.children_offset + i]), base_type)) {
                    error(node.token, "Элементы массива должны быть одного типа");
                }
            }
            result = type_table.register_type({TypeKind::Array, 0, node.children_count, base_type});
            break;
        }
        case Parser::NodeType::StructLiteral: {
            Lexer::IdentId struct_name = nodes[child_indices[node.children_offset]].token.data;
            TypeId struct_id = type_table.find_by_name(struct_name);
            if (struct_id == 0) error(node.token, "Неизвестная структура для инициализации");
            
            // Упрощенная проверка количества полей
            if (node.children_count - 1 != struct_sizes[struct_id]) {
                error(node.token, "Неверное количество полей при инициализации структуры");
            }
            result = struct_id;
            break;
        }"""
content = content.replace("        case Parser::NodeType::ExprStmt: {", literals_code + "\n        case Parser::NodeType::ExprStmt: {")

# 8. Fix Call block bypass for Builtins and remove local resolve_alias
call_block = """        case Parser::NodeType::Call: {
            Parser::NodeId callee_id = child_indices[node.children_offset];
            Lexer::IdentId func_name = nodes[callee_id].token.data;
            std::string fname_str = std::string(pool.get(func_name));

            if (fname_str == "печать" || fname_str == "ввод" || fname_str == "выход" || fname_str == "паника" || fname_str == "утверждение") {
                for (uint32_t i = 1; i < node.children_count; ++i) {
                    check(child_indices[node.children_offset + i]);
                }
                if (fname_str == "ввод") result = type_table.get_builtin(TypeKind::String);
                else result = type_table.get_builtin(TypeKind::Void);
                break;
            }

            if (!functions.count(func_name)) error(node.token, "Вызов неизвестной функции");
            
            auto& sig = functions[func_name];
            if (node.children_count - 1 != sig.param_types.size()) {
                error(node.token, "Неверное количество аргументов");
            }
            for (uint32_t i = 1; i < node.children_count; ++i) {
                TypeId arg_type = check(child_indices[node.children_offset + i]);
                if (!types_compatible(arg_type, sig.param_types[i - 1])) error(node.token, "Несоответствие типа аргумента");
            }
            result = sig.return_type;
            break;
        }"""
old_call_block = re.compile(r'case Parser::NodeType::Call: \{.*?break;\s*\}', re.DOTALL)
content = old_call_block.sub(call_block, content, count=1)

# 9. Fix Return block
ret_resolve_alias = """            auto resolve_alias = [&](TypeId id) {
                while (type_table.get_type(id).kind == TypeKind::Alias) {
                    id = type_table.get_type(id).base_type;
                }
                return id;
            };
            
            if (resolve_alias(ret_type) != resolve_alias(current_func_return_type)) {"""
ret_replacement = '            if (!types_compatible(ret_type, current_func_return_type)) {'
content = content.replace(ret_resolve_alias, ret_replacement)

# 10. Fix AssignStmt block
assign_resolve_alias = """        case Parser::NodeType::AssignStmt: {
            Parser::NodeId target_id = child_indices[node.children_offset];
            Parser::NodeId val_id = child_indices[node.children_offset + 1];
            
            TypeId target_type = check(target_id);
            TypeId val_type = check(val_id);
            
            auto resolve_alias = [&](TypeId id) {
                while (type_table.get_type(id).kind == TypeKind::Alias) {
                    id = type_table.get_type(id).base_type;
                }
                return id;
            };
            
            if (resolve_alias(target_type) != resolve_alias(val_type)) error(node.token, "Несоответствие типов при присваивании");"""
assign_replacement = """        case Parser::NodeType::AssignStmt: {
            Parser::NodeId target_id = child_indices[node.children_offset];
            Parser::NodeId val_id = child_indices[node.children_offset + 1];
            
            TypeId target_type = check(target_id);
            TypeId val_type = check(val_id);
            
            if (!types_compatible(target_type, val_type)) error(node.token, "Несоответствие типов при присваивании");"""
content = content.replace(assign_resolve_alias, assign_replacement)


with open("src/Semantic.cpp", "w") as f:
    f.write(content)

