with open("src/Interpreter.cpp", "r") as f: c = f.read()

# 1. VarDecl in eval
c = c.replace("""    case Parser::NodeType::VarDecl: {
        Value val;
        if (node.extra_data > 0) {
            // Выделяем память под массив в Арене
            val.kind = Semantic::TypeKind::Array;
            val.as.arr = static_cast<Value*>(arena.alloc(sizeof(Value) * node.extra_data, alignof(Value)));
            for(uint32_t i = 0; i < node.extra_data; ++i) val.as.arr[i] = Value(); // Инициализация нулями
        } else if (child_indices[node.children_offset] != Parser::InvalidNode) {
            val = eval(child_indices[node.children_offset]);
        }
        env.define(node.token.data, val);
        break;
    }""", """    case Parser::NodeType::VarDecl: {
        Value val = eval(child_indices[node.children_offset]);
        env.define(node.token.data, val);
        break;
    }""")

# 2. MemberAccess in eval
c = c.replace("""    case Parser::NodeType::MemberAccess: {
        Value obj_val = eval(child_indices[node.children_offset]);
        uint32_t offset = node.extra_data & 0x7FFFFFFF;
        if (node.extra_data & 0x80000000) {
            Value v; v.kind = Semantic::TypeKind::Array;
            v.as.arr = obj_val.as.arr + offset;
            return v;
        }
        return obj_val.as.arr[offset];
    }""", """    case Parser::NodeType::MemberAccess: {
        Value obj_val = eval(child_indices[node.children_offset]);
        uint32_t offset = node.extra_data;
        return obj_val.as.arr[offset];
    }""")

# 3. Assign in eval
c = c.replace("case Parser::NodeType::Assign: {", "case Parser::NodeType::AssignStmt: {")

# 4. MemberAccess inside Assign
c = c.replace("""        } else if (nodes[target_id].type == Parser::NodeType::MemberAccess) {
            Value obj_val = eval(child_indices[nodes[target_id].children_offset]);
            uint32_t offset = nodes[target_id].extra_data & 0x7FFFFFFF;
            obj_val.as.arr[offset] = val;
        }""", """        } else if (nodes[target_id].type == Parser::NodeType::MemberAccess) {
            Value obj_val = eval(child_indices[nodes[target_id].children_offset]);
            uint32_t offset = nodes[target_id].extra_data;
            obj_val.as.arr[offset] = val;
        }""")

# 5. Call in eval
c = c.replace("""        case Parser::NodeType::Call: {
            Parser::NodeId callee_id = child_indices[node.children_offset];
            Lexer::IdentId func_name = nodes[callee_id].token.data;
            if (!functions.count(func_name)) panic("Функция не найдена в рантайме");

            Parser::NodeId func_node_id = functions[func_name];
            const auto& func_node = nodes[func_node_id];

            std::vector<Value> args;
            for (uint32_t i = 1; i < node.children_count; ++i) {
                args.push_back(eval(child_indices[node.children_offset + i]));
            }

            // Создаем новый фрейм стека вызовов
            auto old_scopes = env.get_scopes();
            std::vector<std::unordered_map<Lexer::IdentId, Value>> new_scopes;
            new_scopes.push_back(old_scopes[0]); // Глобальная область
            new_scopes.push_back({}); // Локальная область функции
            env.set_scopes(new_scopes);

            for (uint32_t i = 2; i < func_node.children_count; i += 2) {
                Lexer::IdentId arg_name = nodes[child_indices[func_node.children_offset + i + 1]].token.data;
                env.define(arg_name, args[(i-2)/2]);
            }

            execute(child_indices[func_node.children_offset + 1]);

            Value ret_val = return_value;
            should_return = false;
            return_value = Value();

            env.set_scopes(old_scopes); // Восстанавливаем фрейм
            return ret_val;
        }""", """        case Parser::NodeType::Call: {
            Parser::NodeId callee_id = child_indices[node.children_offset];
            Lexer::IdentId func_name = nodes[callee_id].token.data;
            std::string fname_str = std::string(pool.get(func_name));
            
            if (fname_str == "печать" || fname_str == "ввод" || fname_str == "выход" || fname_str == "паника" || fname_str == "утверждение") {
                std::vector<Value> args;
                for (uint32_t i = 1; i < node.children_count; ++i) {
                    args.push_back(eval(child_indices[node.children_offset + i]));
                }
                if (fname_str == "печать") {
                    if (args[0].kind == Semantic::TypeKind::Int) std::cout << args[0].as.i64 << "\\n";
                    else if (args[0].kind == Semantic::TypeKind::Float) std::cout << args[0].as.f64 << "\\n";
                    else if (args[0].kind == Semantic::TypeKind::Bool) std::cout << (args[0].as.b ? "истина" : "ложь") << "\\n";
                    else if (args[0].kind == Semantic::TypeKind::String) std::cout << args[0].as.s << "\\n";
                    return Value();
                } else if (fname_str == "ввод") {
                    std::string input_str;
                    std::getline(std::cin, input_str);
                    char* mem = static_cast<char*>(arena.alloc(input_str.size() + 1, 1));
                    std::memcpy(mem, input_str.data(), input_str.size());
                    mem[input_str.size()] = '\\0';
                    Value v; v.kind = Semantic::TypeKind::String;
                    v.as.s = mem;
                    return v;
                } else if (fname_str == "выход") {
                    std::exit(args[0].as.i64);
                } else if (fname_str == "паника") {
                    panic(args[0].as.s);
                } else if (fname_str == "утверждение") {
                    if (!args[0].as.b) panic("Утверждение ложно (assert failed)");
                    return Value();
                }
            }

            if (!functions.count(func_name)) panic("Функция не найдена в рантайме: " + fname_str);

            Parser::NodeId func_node_id = functions[func_name];
            const auto& func_node = nodes[func_node_id];

            std::vector<Value> args;
            for (uint32_t i = 1; i < node.children_count; ++i) {
                args.push_back(eval(child_indices[node.children_offset + i]));
            }

            auto old_scopes = env.get_scopes();
            std::vector<std::unordered_map<Lexer::IdentId, Value>> new_scopes;
            new_scopes.push_back(old_scopes[0]);
            new_scopes.push_back({});
            env.set_scopes(new_scopes);

            for (uint32_t i = 2; i < func_node.children_count; i += 2) {
                Lexer::IdentId arg_name = nodes[child_indices[func_node.children_offset + i + 1]].token.data;
                env.define(arg_name, args[(i-2)/2]);
            }

            execute(child_indices[func_node.children_offset + 1]);

            Value ret_val = return_value;
            should_return = false;
            return_value = Value();

            env.set_scopes(old_scopes);
            return ret_val;
        }""")

# 6. BuiltinInput in eval
c = c.replace("""        case Parser::NodeType::BuiltinInput: {
            std::string input_str;
            std::getline(std::cin, input_str);

            // Выделяем память под строку в runtime_arena
            char* mem = static_cast<char*>(arena.alloc(input_str.size() + 1, 1));
            std::memcpy(mem, input_str.data(), input_str.size());
            mem[input_str.size()] = '\\0';

            Value v; v.kind = Semantic::TypeKind::String;
            v.as.s = mem;
            return v;
        }""", "")

# 7. Add ArrayLiteral & StructLiteral to eval
c = c.replace("    case Parser::NodeType::Identifier: {", """    case Parser::NodeType::ArrayLiteral: {
        Value val;
        val.kind = Semantic::TypeKind::Array;
        val.as.arr = static_cast<Value*>(arena.alloc(sizeof(Value) * node.children_count, alignof(Value)));
        for (uint32_t i = 0; i < node.children_count; ++i) {
            val.as.arr[i] = eval(child_indices[node.children_offset + i]);
        }
        return val;
    }
    case Parser::NodeType::StructLiteral: {
        Value val;
        val.kind = Semantic::TypeKind::Struct;
        val.as.arr = static_cast<Value*>(arena.alloc(sizeof(Value) * node.children_count, alignof(Value)));
        for (uint32_t i = 0; i < node.children_count; ++i) {
            val.as.arr[i] = eval(child_indices[node.children_offset + i]);
        }
        return val;
    }
    case Parser::NodeType::Identifier: {""")

# 8. FuncDecl in execute
c = c.replace("""        case Parser::NodeType::FuncDecl: {
            functions[node.token.data] = id;
            break;
        }""", """        case Parser::NodeType::NamespaceDecl: {
            std::string old_prefix = namespace_prefix;
            namespace_prefix += std::string(pool.get(node.token.data)) + "::";
            for (uint32_t i = 0; i < node.children_count; ++i) {
                execute(child_indices[node.children_offset + i]);
            }
            namespace_prefix = old_prefix;
            break;
        }
        case Parser::NodeType::FuncDecl: {
            Lexer::IdentId name = pool.intern(namespace_prefix + std::string(pool.get(node.token.data)));
            functions[name] = id;
            break;
        }""")

# 9. BuiltinExit, BuiltinPanic, BuiltinAssert in execute
c = c.replace("""        case Parser::NodeType::BuiltinExit: {
            Value arg = eval(child_indices[node.children_offset]);
            std::exit(arg.as.i64);
        }
        case Parser::NodeType::BuiltinPanic: {
            Value arg = eval(child_indices[node.children_offset]);
            panic(arg.as.s);
            break;
        }
        case Parser::NodeType::BuiltinAssert: {
            Value arg = eval(child_indices[node.children_offset]);
            if (!arg.as.b) panic("Утверждение ложно (assert failed)");
            break;
        }""", """        case Parser::NodeType::Call: {
            eval(id);
            break;
        }
        case Parser::NodeType::AssignStmt: {
            eval(id);
            break;
        }""")

# 10. VarDecl in execute
c = c.replace("""    case Parser::NodeType::VarDecl: {
        Value val;
        if (node.extra_data > 0) {
            // Выделяем память под массив в Арене
            val.kind = Semantic::TypeKind::Array;
            val.as.arr = static_cast<Value*>(arena.alloc(sizeof(Value) * node.extra_data, alignof(Value)));
            for(uint32_t i = 0; i < node.extra_data; ++i) val.as.arr[i] = Value(); // Инициализация нулями
        } else if (child_indices[node.children_offset] != Parser::InvalidNode) {
            val = eval(child_indices[node.children_offset]);
        }
        env.define(node.token.data, val);
        break;
    }""", """    case Parser::NodeType::VarDecl: {
        Value val = eval(child_indices[node.children_offset]);
        env.define(node.token.data, val);
        break;
    }""")

# 11. ExprStmt in execute
c = c.replace("""        case Parser::NodeType::ExprStmt: {
            Parser::NodeId expr_id = child_indices[node.children_offset];
            Value val = eval(expr_id);

            // Чистая обработка функции "печать"
            if (node.token.type == Lexer::TokenType::KwPrint) {
                switch (val.kind) {
                    case Semantic::TypeKind::Int: std::cout << val.as.i64 << "\\n"; break;
                    case Semantic::TypeKind::Float: std::cout << val.as.f64 << "\\n"; break;
                    case Semantic::TypeKind::Bool: std::cout << (val.as.b ? "истина" : "ложь") << "\\n"; break;
                    case Semantic::TypeKind::String: std::cout << val.as.s << "\\n"; break;
                    default: std::cout << "void\\n"; break;
                }
            }
            break;
        }""", """        case Parser::NodeType::ExprStmt: {
            eval(child_indices[node.children_offset]);
            break;
        }""")

with open("src/Interpreter.cpp", "w") as f:
    f.write(c)
