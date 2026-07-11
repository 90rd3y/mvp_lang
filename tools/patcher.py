import re

with open("src/Interpreter.cpp", "r") as f:
    c = f.read()

# 1. VarDecl in eval -> NO, VarDecl is in execute
# 2. MemberAccess in eval
c = re.sub(r'case Parser::NodeType::MemberAccess:\s*\{\s*Value obj_val = eval\(child_indices\[node\.children_offset\]\);\s*uint32_t offset = node\.extra_data & 0x7FFFFFFF;\s*if \(node\.extra_data & 0x80000000\) \{\s*Value v; v\.kind = Semantic::TypeKind::Array;\s*v\.as\.arr = obj_val\.as\.arr \+ offset;\s*return v;\s*\}\s*return obj_val\.as\.arr\[offset\];\s*\}',
'''case Parser::NodeType::MemberAccess: {
        Value obj_val = eval(child_indices[node.children_offset]);
        uint32_t offset = node.extra_data;
        return obj_val.as.arr[offset];
    }''', c)

# 3. Assign to AssignStmt
c = re.sub(r'case Parser::NodeType::Assign:\s*\{', 'case Parser::NodeType::AssignStmt: {', c)

# 4. MemberAccess inside Assign
c = re.sub(r'\} else if \(nodes\[target_id\]\.type == Parser::NodeType::MemberAccess\) \{\s*Value obj_val = eval\(child_indices\[nodes\[target_id\]\.children_offset\]\);\s*uint32_t offset = nodes\[target_id\]\.extra_data & 0x7FFFFFFF;\s*obj_val\.as\.arr\[offset\] = val;\s*\}',
'''} else if (nodes[target_id].type == Parser::NodeType::MemberAccess) {
            Value obj_val = eval(child_indices[nodes[target_id].children_offset]);
            uint32_t offset = nodes[target_id].extra_data;
            obj_val.as.arr[offset] = val;
        }''', c)

# 5. Call in eval
c = re.sub(r'case Parser::NodeType::Call:\s*\{\s*Parser::NodeId callee_id = child_indices\[node\.children_offset\];\s*Lexer::IdentId func_name = nodes\[callee_id\]\.token\.data;\s*if \(!functions\.count\(func_name\)\) panic\("Функция не найдена в рантайме"\);\s*Parser::NodeId func_node_id = functions\[func_name\];\s*const auto& func_node = nodes\[func_node_id\];\s*std::vector<Value> args;\s*for \(uint32_t i = 1; i < node\.children_count; \+\+i\) \{\s*args\.push_back\(eval\(child_indices\[node\.children_offset \+ i\]\)\);\s*\}\s*// Создаем новый фрейм стека вызовов\s*auto old_scopes = env\.get_scopes\(\);\s*std::vector<std::unordered_map<Lexer::IdentId, Value>> new_scopes;\s*new_scopes\.push_back\(old_scopes\[0\]\); // Глобальная область\s*new_scopes\.push_back\(\{\}\); // Локальная область функции\s*env\.set_scopes\(new_scopes\);\s*for \(uint32_t i = 2; i < func_node\.children_count; i \+= 2\) \{\s*Lexer::IdentId arg_name = nodes\[child_indices\[func_node\.children_offset \+ i \+ 1\]\]\.token\.data;\s*env\.define\(arg_name, args\[\(i-2\)/2\]\);\s*\}\s*execute\(child_indices\[func_node\.children_offset \+ 1\]\);\s*Value ret_val = return_value;\s*should_return = false;\s*return_value = Value\(\);\s*env\.set_scopes\(old_scopes\); // Восстанавливаем фрейм\s*return ret_val;\s*\}',
'''case Parser::NodeType::Call: {
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
        }''', c)

# 6. BuiltinInput
c = re.sub(r'case Parser::NodeType::BuiltinInput:\s*\{\s*std::string input_str;\s*std::getline\(std::cin, input_str\);\s*// Выделяем память под строку в runtime_arena\s*char\* mem = static_cast<char\*>\(arena\.alloc\(input_str\.size\(\) \+ 1, 1\)\);\s*std::memcpy\(mem, input_str\.data\(\), input_str\.size\(\)\);\s*mem\[input_str\.size\(\)\] = \'\\0\';\s*Value v; v\.kind = Semantic::TypeKind::String;\s*v\.as\.s = mem;\s*return v;\s*\}', '', c)

# 7. ArrayLiteral & StructLiteral
c = c.replace('case Parser::NodeType::Identifier:', '''case Parser::NodeType::ArrayLiteral: {
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
    case Parser::NodeType::Identifier:''')

# 8. NamespaceDecl & FuncDecl in execute
c = re.sub(r'case Parser::NodeType::FuncDecl:\s*\{\s*functions\[node\.token\.data\] = id;\s*break;\s*\}',
'''case Parser::NodeType::NamespaceDecl: {
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
        }''', c)

# 9. BuiltinExit, BuiltinPanic, BuiltinAssert in execute
c = re.sub(r'case Parser::NodeType::BuiltinExit:\s*\{\s*Value arg = eval\(child_indices\[node\.children_offset\]\);\s*std::exit\(arg\.as\.i64\);\s*\}\s*case Parser::NodeType::BuiltinPanic:\s*\{\s*Value arg = eval\(child_indices\[node\.children_offset\]\);\s*panic\(arg\.as\.s\);\s*break;\s*\}\s*case Parser::NodeType::BuiltinAssert:\s*\{\s*Value arg = eval\(child_indices\[node\.children_offset\]\);\s*if \(!arg\.as\.b\) panic\("Утверждение ложно \(assert failed\)"\);\s*break;\s*\}',
'''case Parser::NodeType::Call: {
            eval(id);
            break;
        }
        case Parser::NodeType::AssignStmt: {
            eval(id);
            break;
        }''', c)

# 10. VarDecl in execute
c = re.sub(r'case Parser::NodeType::VarDecl:\s*\{\s*Value val;\s*if \(node\.extra_data > 0\) \{\s*// Выделяем память под массив в Арене\s*val\.kind = Semantic::TypeKind::Array;\s*val\.as\.arr = static_cast<Value\*>\(arena\.alloc\(sizeof\(Value\) \* node\.extra_data, alignof\(Value\)\)\);\s*for\(uint32_t i = 0; i < node\.extra_data; \+\+i\) val\.as\.arr\[i\] = Value\(\); // Инициализация нулями\s*\} else if \(child_indices\[node\.children_offset\] != Parser::InvalidNode\) \{\s*val = eval\(child_indices\[node\.children_offset\]\);\s*\}\s*env\.define\(node\.token\.data, val\);\s*break;\s*\}',
'''case Parser::NodeType::VarDecl: {
        Value val = eval(child_indices[node.children_offset]);
        env.define(node.token.data, val);
        break;
    }''', c)

# 11. ExprStmt in execute
c = re.sub(r'case Parser::NodeType::ExprStmt:\s*\{\s*Parser::NodeId expr_id = child_indices\[node\.children_offset\];\s*Value val = eval\(expr_id\);\s*// Чистая обработка функции "печать"\s*if \(node\.token\.type == Lexer::TokenType::KwPrint\) \{\s*switch \(val\.kind\) \{\s*case Semantic::TypeKind::Int: std::cout << val\.as\.i64 << "\\n"; break;\s*case Semantic::TypeKind::Float: std::cout << val\.as\.f64 << "\\n"; break;\s*case Semantic::TypeKind::Bool: std::cout << \(val\.as\.b \? "истина" : "ложь"\) << "\\n"; break;\s*case Semantic::TypeKind::String: std::cout << val\.as\.s << "\\n"; break;\s*default: std::cout << "void\\n"; break;\s*\}\s*\}\s*break;\s*\}',
'''case Parser::NodeType::ExprStmt: {
            eval(child_indices[node.children_offset]);
            break;
        }''', c)


with open("src/Interpreter.cpp", "w") as f:
    f.write(c)
