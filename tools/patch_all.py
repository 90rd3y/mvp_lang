import re

with open("src/Interpreter.cpp", "r") as f:
    content = f.read()

# Fix Assign -> AssignStmt
content = content.replace("case Parser::NodeType::Assign:", "case Parser::NodeType::AssignStmt:")

# Replace the whole switch in `execute` from BuiltinExit to ExprStmt
execute_pattern = re.compile(r'case Parser::NodeType::BuiltinExit:.*?case Parser::NodeType::ExprStmt: \{.*?\}', re.DOTALL)

execute_replacement = """        case Parser::NodeType::Call: {
            eval(id);
            break;
        }
        case Parser::NodeType::AssignStmt: {
            eval(id);
            break;
        }
        case Parser::NodeType::ExprStmt: {
            eval(child_indices[node.children_offset]);
            break;
        }"""
content = execute_pattern.sub(execute_replacement, content, count=1)

# Now fix Call in `eval()`
eval_call_pattern = re.compile(r'case Parser::NodeType::Call: \{.*?return Value\(\);\s*\}', re.DOTALL)
eval_call_replacement = """        case Parser::NodeType::Call: {
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
                    std::string input;
                    std::cin >> input;
                    Value v; v.kind = Semantic::TypeKind::String;
                    v.as.s = pool.intern(input).data();
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
                Lexer::IdentId param_name = nodes[child_indices[func_node.children_offset + i + 1]].token.data;
                env.define(param_name, args[i/2 - 1]);
            }

            try {
                execute(child_indices[func_node.children_offset + 1]);
            } catch (Value ret_val) {
                env.set_scopes(old_scopes);
                return ret_val;
            }

            env.set_scopes(old_scopes);
            return Value();
        }"""
content = eval_call_pattern.sub(eval_call_replacement, content, count=1)

# Fix VarDecl inside `execute`
var_decl_exec = """        case Parser::NodeType::VarDecl: {
            Value val = eval(child_indices[node.children_offset]);
            env.define(node.token.data, val);
            break;
        }"""
content = re.sub(r'case Parser::NodeType::VarDecl: \{.*?break;\s*\}', var_decl_exec, content, flags=re.DOTALL)

with open("src/Interpreter.cpp", "w") as f:
    f.write(content)
