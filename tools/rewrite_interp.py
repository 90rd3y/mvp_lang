import re
with open("src/Interpreter.cpp", "r") as f:
    c = f.read()

# 1. VarDecl in eval
c = re.sub(r'case Parser::NodeType::VarDecl: \{.*?break;\s*\}', """        case Parser::NodeType::VarDecl: {
            Value val = eval(child_indices[node.children_offset]);
            env.define(node.token.data, val);
            break;
        }""", c, count=1, flags=re.DOTALL)

# 2. MemberAccess in eval
c = re.sub(r'case Parser::NodeType::MemberAccess: \{.*?return obj_val\.as\.arr\[offset\];\s*\}', """        case Parser::NodeType::MemberAccess: {
            Value obj_val = eval(child_indices[node.children_offset]);
            uint32_t offset = node.extra_data;
            return obj_val.as.arr[offset];
        }""", c, count=1, flags=re.DOTALL)

# 3. Assign in eval
c = c.replace("case Parser::NodeType::Assign: {", "case Parser::NodeType::AssignStmt: {")

# 4. MemberAccess inside Assign
c = re.sub(r'\} else if \(nodes\[target_id\]\.type == Parser::NodeType::MemberAccess\) \{.*?obj_val\.as\.arr\[offset\] = val;\s*\}', """            } else if (nodes[target_id].type == Parser::NodeType::MemberAccess) {
                Value obj_val = eval(child_indices[nodes[target_id].children_offset]);
                uint32_t offset = nodes[target_id].extra_data;
                obj_val.as.arr[offset] = val;
            }""", c, count=1, flags=re.DOTALL)

# 5. Call in eval
c = re.sub(r'case Parser::NodeType::Call: \{.*?return ret_val;\s*\}', """        case Parser::NodeType::Call: {
            Parser::NodeId callee_id = child_indices[node.children_offset];
            Lexer::IdentId func_name = nodes[callee_id].token.data;
            std::string fname_str = std::string(pool.get(func_name));
            
            if (fname_str == "печать" || fname_str == "ввод" || fname_str == "выход" || fname_str == "паника" || fname_str == "утверждение") {
                std::vector<Value> args;
                for (uint32_t i = 1; i < node.children_count; ++i) {
                    args.push_back(eval(child_indices[node.children_offset + i]));
                }
                if (fname_str == "печать") {
                    if (args[0].kind == Semantic::TypeKind::Int) std::cout << args[0].as.i64 << std::endl;
                    else if (args[0].kind == Semantic::TypeKind::Float) std::cout << args[0].as.f64 << std::endl;
                    else if (args[0].kind == Semantic::TypeKind::Bool) std::cout << (args[0].as.b ? "Истина" : "Ложь") << std::endl;
                    else if (args[0].kind == Semantic::TypeKind::String) std::cout << args[0].as.s << std::endl;
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
        }""", c, count=1, flags=re.DOTALL)

# 6. Remove BuiltinInput
c = re.sub(r'case Parser::NodeType::BuiltinInput: \{.*?return v;\s*\}', '', c, count=1, flags=re.DOTALL)

# 7. Add NamespaceDecl and update FuncDecl
c = re.sub(r'case Parser::NodeType::FuncDecl: \{.*?break;\s*\}', """        case Parser::NodeType::NamespaceDecl: {
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
        }""", c, count=1, flags=re.DOTALL)

# 8. BuiltinExit, BuiltinPanic, BuiltinAssert to Call, AssignStmt
c = re.sub(r'case Parser::NodeType::BuiltinExit: \{.*?case Parser::NodeType::ExprStmt: \{', """        case Parser::NodeType::Call: {
            eval(id);
            break;
        }
        case Parser::NodeType::AssignStmt: {
            eval(id);
            break;
        }
        case Parser::NodeType::ExprStmt: {""", c, count=1, flags=re.DOTALL)

# 9. Fix ExprStmt in execute
c = re.sub(r'case Parser::NodeType::ExprStmt: \{.*?break;\s*\}', """        case Parser::NodeType::ExprStmt: {
            eval(child_indices[node.children_offset]);
            break;
        }""", c, count=1, flags=re.DOTALL)

# 10. Fix VarDecl in execute
c = re.sub(r'case Parser::NodeType::VarDecl: \{.*?break;\s*\}', """    case Parser::NodeType::VarDecl: {
        Value val = eval(child_indices[node.children_offset]);
        env.define(node.token.data, val);
        break;
    }""", c, count=1, flags=re.DOTALL)

with open("src/Interpreter.cpp", "w") as f:
    f.write(c)
