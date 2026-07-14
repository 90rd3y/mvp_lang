#include "../inc/Interpreter.hpp"
#include <cstring>
#include <cmath>
#include <iostream>

namespace Interpreter {

// --- Environment ---

void Environment::enter_scope() { scopes.push_back({}); }
void Environment::exit_scope() { scopes.pop_back(); }

void Environment::define(Lexer::IdentId name, Value val) {
  scopes.back()[name] = val;
}

void Environment::assign(Lexer::IdentId name, Value val) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    if (it->count(name)) {
      (*it)[name] = val;
      return;
    }
  }
}

Value Environment::get(Lexer::IdentId name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    if (it->count(name))
      return (*it)[name];
  }
  return Value();
}

// --- VM ---

VM::VM(const std::vector<Parser::ASTNode> &n,
       const std::vector<Parser::NodeId> &c, Lexer::StringPool &p,
       Memory::Arena &a)
    : nodes(n), child_indices(c), pool(p), arena(a) {
  env.enter_scope(); // Глобальный контекст
}

void VM::update_line(Parser::NodeId id) {
  if (id != Parser::InvalidNode && id < nodes.size() && nodes[id].token.line > 0) {
    current_line = nodes[id].token.line;
    if (!call_stack.empty()) {
      call_stack.back().call_line = current_line;
    }
  }
}

void VM::panic(const std::string &msg) {
  std::cerr << "Ошибка времени выполнения (строка " << current_line << "): " << msg << std::endl;
  if (!call_stack.empty()) {
    std::cerr << "Стек вызовов:" << std::endl;
    for (const auto &frame : call_stack) {
      std::cerr << "  в функции " << frame.func_name << " (строка " << frame.call_line << ")" << std::endl;
    }
  }
  std::exit(1);
}

// Глубокое (по значению) сравнение. Для Array/Struct обход рекурсивный: элементы/поля
// сами являются полноценными Value со своим kind/count, поэтому вложенность массивов
// в структуры и структур в массивы поддерживается без специального кода (см. §9, §10
// TYPE_SYSTEM.md). Статический анализ уже гарантирует, что a.kind == b.kind для любого
// достижимого сюда вызова (см. check_binary в Semantic.cpp), поэтому явно на это не
// проверяем.
bool VM::values_equal(const Value &a, const Value &b) {
  switch (a.kind) {
  case Semantic::TypeKind::Int:
  case Semantic::TypeKind::Char:
    return a.as.i64 == b.as.i64;
  case Semantic::TypeKind::Float:
    return a.as.f64 == b.as.f64;
  case Semantic::TypeKind::Bool:
    return a.as.b == b.as.b;
  case Semantic::TypeKind::String:
    return std::strcmp(a.as.s, b.as.s) == 0;
  case Semantic::TypeKind::Array:
  case Semantic::TypeKind::Struct: {
    if (a.count != b.count) return false;
    for (uint32_t i = 0; i < a.count; ++i) {
      if (!values_equal(a.as.arr[i], b.as.arr[i])) return false;
    }
    return true;
  }
  default:
    return true; // Void и т.п. — сравнивать нечего.
  }
}

void VM::check_index_bounds(const Value &arr_val, const Value &idx_val) {
  if (idx_val.as.i64 < 0 || static_cast<uint64_t>(idx_val.as.i64) >= arr_val.count) {
    panic("Индекс " + std::to_string(idx_val.as.i64) + " выходит за границы массива (размер " +
          std::to_string(arr_val.count) + ")");
  }
}

Value VM::eval(Parser::NodeId id) {
  if (id == Parser::InvalidNode)
    return Value();
  update_line(id);
  const auto &node = nodes[id];

  switch (node.type) {
  case Parser::NodeType::LiteralInt: {
    Value v;
    v.kind = Semantic::TypeKind::Int;
    v.as.i64 = std::stoll(std::string(node.token.lexeme));
    return v;
  }
  case Parser::NodeType::LiteralFloat: {
    Value v;
    v.kind = Semantic::TypeKind::Float;
    v.as.f64 = std::stod(std::string(node.token.lexeme));
    return v;
  }
  case Parser::NodeType::LiteralBool: {
    Value v;
    v.kind = Semantic::TypeKind::Bool;
    v.as.b = (node.token.type == Lexer::TokenType::KwTrue);
    return v;
  }
  case Parser::NodeType::LiteralString: {
    Value v;
    v.kind = Semantic::TypeKind::String;
    v.as.s = pool.get(node.token.data).data();
    return v;
  }
  case Parser::NodeType::LiteralChar: {
    Value v;
    v.kind = Semantic::TypeKind::Char;
    std::string_view lexeme = pool.get(node.token.data);
    if (lexeme.empty()) {
      v.as.i64 = 0;
    } else if (lexeme[0] == '\\') {
      // Допустимый набор экранирующих последовательностей (см. Lexer::Scanner::scan_escape):
      // \н -> '\n', \п -> '\t', \0 -> '\0', \\ -> '\\', \' -> '\'', \" -> '"'.
      if (lexeme.size() > 1) {
        unsigned char e1 = static_cast<unsigned char>(lexeme[1]);
        if (e1 == '\\') {
          v.as.i64 = '\\';
        } else if (e1 == '\'') {
          v.as.i64 = '\'';
        } else if (e1 == '"') {
          v.as.i64 = '\"';
        } else if (e1 == '0') {
          v.as.i64 = '\0';
        } else if (e1 == 0xD0 && lexeme.size() > 2) {
          unsigned char e2 = static_cast<unsigned char>(lexeme[2]);
          if (e2 == 0xBD) v.as.i64 = '\n';      // \н
          else if (e2 == 0xBF) v.as.i64 = '\t'; // \п
          else v.as.i64 = e1;
        } else {
          v.as.i64 = e1;
        }
      } else {
        v.as.i64 = '\\';
      }
    } else {
      unsigned char b1 = lexeme[0];
      if (b1 < 128) {
        v.as.i64 = b1;
      } else if (b1 < 224 && lexeme.size() >= 2) {
        v.as.i64 = ((b1 & 0x1F) << 6) | (lexeme[1] & 0x3F);
      } else if (b1 < 240 && lexeme.size() >= 3) {
        v.as.i64 = ((b1 & 0x0F) << 12) | ((lexeme[1] & 0x3F) << 6) | (lexeme[2] & 0x3F);
      } else if (b1 < 248 && lexeme.size() >= 4) {
        v.as.i64 = ((b1 & 0x07) << 18) | ((lexeme[1] & 0x3F) << 12) | ((lexeme[2] & 0x3F) << 6) | (lexeme[3] & 0x3F);
      } else {
        v.as.i64 = b1;
      }
    }
    return v;
  }
  case Parser::NodeType::ArrayLiteral: {
      Value val;
      val.kind = Semantic::TypeKind::Array;
      val.count = node.children_count;
      val.as.arr = static_cast<Value*>(arena.alloc(sizeof(Value) * node.children_count, alignof(Value)));
      for (uint32_t i = 0; i < node.children_count; ++i) {
          val.as.arr[i] = eval(child_indices[node.children_offset + i]);
      }
      return val;
  }
  case Parser::NodeType::StructLiteral: {
      Value val;
      val.kind = Semantic::TypeKind::Struct;
      uint32_t field_count = node.children_count - 1;
      val.count = field_count;
      val.as.arr = static_cast<Value*>(arena.alloc(sizeof(Value) * field_count, alignof(Value)));
      for (uint32_t i = 0; i < field_count; ++i) {
          val.as.arr[i] = eval(child_indices[node.children_offset + 1 + i]);
      }
      return val;
  }
  case Parser::NodeType::Identifier: {
    return env.get(node.token.data);
  }
  case Parser::NodeType::BinaryOp: {
    Value left = eval(child_indices[node.children_offset]);
    Value right = eval(child_indices[node.children_offset + 1]);
    Value res;
    res.kind = left.kind;

    switch (node.token.type) {
    case Lexer::TokenType::Plus:
      if (left.kind == Semantic::TypeKind::Int)
        res.as.i64 = left.as.i64 + right.as.i64;
      else
        res.as.f64 = left.as.f64 + right.as.f64;
      break;
    case Lexer::TokenType::Minus:
      if (left.kind == Semantic::TypeKind::Int)
        res.as.i64 = left.as.i64 - right.as.i64;
      else
        res.as.f64 = left.as.f64 - right.as.f64;
      break;
    case Lexer::TokenType::Star:
      if (left.kind == Semantic::TypeKind::Int)
        res.as.i64 = left.as.i64 * right.as.i64;
      else
        res.as.f64 = left.as.f64 * right.as.f64;
      break;
    case Lexer::TokenType::Slash:
      if (right.as.i64 == 0 && right.as.f64 == 0.0)
        panic("Деление на ноль");
      if (left.kind == Semantic::TypeKind::Int)
        res.as.i64 = left.as.i64 / right.as.i64;
      else
        res.as.f64 = left.as.f64 / right.as.f64;
      break;
    case Lexer::TokenType::Percent:
      if (right.as.i64 == 0)
        panic("Деление на ноль");
      res.as.i64 = left.as.i64 % right.as.i64;
      break;
    case Lexer::TokenType::EqualEqual:
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = values_equal(left, right);
      break;
    case Lexer::TokenType::BangEqual:
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = !values_equal(left, right);
      break;
    // Статический анализ допускает <, <=, >, >= только между целое/целое и вещ/вещ
    // (см. check_binary в Semantic.cpp), поэтому здесь достаточно разветвления по
    // left.kind: сравнение через .as.i64 для вещ давало неверный результат для двух
    // отрицательных чисел (побитовая интерпретация double как int64 инвертирует
    // порядок при отрицательных значениях, см. §5.1 SEMANTICS.md).
    case Lexer::TokenType::Less:
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = (left.kind == Semantic::TypeKind::Int) ? (left.as.i64 < right.as.i64) : (left.as.f64 < right.as.f64);
      break;
    case Lexer::TokenType::LessEqual:
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = (left.kind == Semantic::TypeKind::Int) ? (left.as.i64 <= right.as.i64) : (left.as.f64 <= right.as.f64);
      break;
    case Lexer::TokenType::Greater:
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = (left.kind == Semantic::TypeKind::Int) ? (left.as.i64 > right.as.i64) : (left.as.f64 > right.as.f64);
      break;
    case Lexer::TokenType::GreaterEqual:
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = (left.kind == Semantic::TypeKind::Int) ? (left.as.i64 >= right.as.i64) : (left.as.f64 >= right.as.f64);
      break;
    default:
      break;
    }
    return res;
  }
  case Parser::NodeType::UnaryOp: {
    // Semantic::check_unary статически гарантирует "-" только для Int/Float и "!"
    // только для Bool, поэтому здесь достаточно различать их по токену оператора.
    Value operand = eval(child_indices[node.children_offset]);
    Value res;
    if (node.token.type == Lexer::TokenType::Minus) {
      res.kind = operand.kind;
      if (operand.kind == Semantic::TypeKind::Int) res.as.i64 = -operand.as.i64;
      else res.as.f64 = -operand.as.f64;
    } else if (node.token.type == Lexer::TokenType::Bang) {
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = !operand.as.b;
    }
    return res;
  }
    case Parser::NodeType::Indexing: {
        Value arr_val = eval(child_indices[node.children_offset]);
        Value idx_val = eval(child_indices[node.children_offset + 1]);
        check_index_bounds(arr_val, idx_val);
        return arr_val.as.arr[idx_val.as.i64];
    }
    case Parser::NodeType::MemberAccess: {
        Value obj_val = eval(child_indices[node.children_offset]);
        uint32_t offset = node.extra_data;
        return obj_val.as.arr[offset];
    }
    case Parser::NodeType::AssignStmt: {
        Parser::NodeId target_id = child_indices[node.children_offset];
        Value val = eval(child_indices[node.children_offset + 1]);

        if (nodes[target_id].type == Parser::NodeType::Identifier) {
            env.assign(nodes[target_id].token.data, val);
        } else if (nodes[target_id].type == Parser::NodeType::Indexing) {
            Value arr_val = eval(child_indices[nodes[target_id].children_offset]);
            Value idx_val = eval(child_indices[nodes[target_id].children_offset + 1]);
            check_index_bounds(arr_val, idx_val);
            arr_val.as.arr[idx_val.as.i64] = val;
        } else if (nodes[target_id].type == Parser::NodeType::MemberAccess) {
            Value obj_val = eval(child_indices[nodes[target_id].children_offset]);
            uint32_t offset = nodes[target_id].extra_data;
            obj_val.as.arr[offset] = val;
        }
        return val;
    }
        case Parser::NodeType::Call: {
            Parser::NodeId callee_id = child_indices[node.children_offset];
            Lexer::IdentId func_name = nodes[callee_id].token.data;
            std::string fname_str = std::string(pool.get(func_name));
            
            if (fname_str == "печать" || fname_str == "ввод" || fname_str == "выход" || fname_str == "паника" || fname_str == "утверждение") {
                std::vector<Value> args;
                for (uint32_t i = 1; i < node.children_count; ++i) {
                    args.push_back(eval(child_indices[node.children_offset + i]));
                }
                if (fname_str == "печать") {
                    if (args[0].kind == Semantic::TypeKind::Int) std::cout << args[0].as.i64 << "\n";
                    else if (args[0].kind == Semantic::TypeKind::Float) std::cout << args[0].as.f64 << "\n";
                    else if (args[0].kind == Semantic::TypeKind::Bool) std::cout << (args[0].as.b ? "истина" : "ложь") << "\n";
                    else if (args[0].kind == Semantic::TypeKind::String) std::cout << args[0].as.s << "\n";
                    else if (args[0].kind == Semantic::TypeKind::Char) {
                        int64_t cp = args[0].as.i64;
                        if (cp < 128) {
                            std::cout << (char)cp << "\n";
                        } else if (cp < 2048) {
                            std::cout << (char)(0xC0 | (cp >> 6)) << (char)(0x80 | (cp & 0x3F)) << "\n";
                        } else if (cp < 65536) {
                            std::cout << (char)(0xE0 | (cp >> 12)) << (char)(0x80 | ((cp >> 6) & 0x3F)) << (char)(0x80 | (cp & 0x3F)) << "\n";
                        } else {
                            std::cout << (char)(0xF0 | (cp >> 18)) << (char)(0x80 | ((cp >> 12) & 0x3F)) << (char)(0x80 | ((cp >> 6) & 0x3F)) << (char)(0x80 | (cp & 0x3F)) << "\n";
                        }
                    }
                    return Value();
                } else if (fname_str == "ввод") {
                    std::string input_str;
                    std::getline(std::cin, input_str);
                    char* mem = static_cast<char*>(arena.alloc(input_str.size() + 1, 1));
                    std::memcpy(mem, input_str.data(), input_str.size());
                    mem[input_str.size()] = '\0';
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
            if (child_indices[func_node.children_offset + 1] == Parser::InvalidNode) {
                panic("Вызов функции без определения (только прототип): " + fname_str);
            }

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

            if (call_stack.size() > 1000) {
                panic("Превышен лимит глубины рекурсии (Stack Overflow)");
            }
            call_stack.push_back({fname_str, func_node.token.line});
            execute(child_indices[func_node.children_offset + 1]);
            if (!call_stack.empty()) call_stack.pop_back();

            Value ret_val = return_value;
            should_return = false;
            return_value = Value();

            // Глобальная область (индекс 0) — общая для всех вызовов, но env.set_scopes
            // выше подменил её на копию. Если тело функции изменило глобальную переменную,
            // перед восстановлением стека вызывающего кода нужно перенести эту копию
            // обратно, иначе изменение потерялось бы сразу после return (см. §3 SEMANTICS.md).
            old_scopes[0] = env.get_scopes()[0];
            env.set_scopes(old_scopes);
            return ret_val;
        }
    case Parser::NodeType::Cast: {
        Lexer::TokenType target_type_tok = nodes[child_indices[node.children_offset]].token.type;
        Value expr_val = eval(child_indices[node.children_offset + 1]);
        
        Value res;
        if (target_type_tok == Lexer::TokenType::KwInt) {
            res.kind = Semantic::TypeKind::Int;
            if (expr_val.kind == Semantic::TypeKind::Float) res.as.i64 = static_cast<int64_t>(expr_val.as.f64);
            else if (expr_val.kind == Semantic::TypeKind::Bool) res.as.i64 = expr_val.as.b ? 1 : 0;
            else res.as.i64 = expr_val.as.i64;
        } else if (target_type_tok == Lexer::TokenType::KwFloat) {
            res.kind = Semantic::TypeKind::Float;
            if (expr_val.kind == Semantic::TypeKind::Int) res.as.f64 = static_cast<double>(expr_val.as.i64);
            else if (expr_val.kind == Semantic::TypeKind::Bool) res.as.f64 = expr_val.as.b ? 1.0 : 0.0;
            else res.as.f64 = expr_val.as.f64;
        } else if (target_type_tok == Lexer::TokenType::KwBool) {
            // is_castable_type допускает как<лог>(...) только от целое/вещ/лог (см. §8
            // SEMANTICS.md); ранее эта ветка вообще отсутствовала и приведение к лог
            // тихо становилось no-op (результат оставался исходным значением as-is).
            res.kind = Semantic::TypeKind::Bool;
            if (expr_val.kind == Semantic::TypeKind::Int) res.as.b = (expr_val.as.i64 != 0);
            else if (expr_val.kind == Semantic::TypeKind::Float) res.as.b = (expr_val.as.f64 != 0.0);
            else res.as.b = expr_val.as.b;
        } else {
            res = expr_val;
        }
        return res;
    }
  default:
    return Value();
  }
}

void VM::execute(Parser::NodeId id) {
  if (id == Parser::InvalidNode || should_return || should_break || should_continue)
    return;
  update_line(id);
  const auto &node = nodes[id];

  switch (node.type) {
        case Parser::NodeType::Program: {
            // В отличие от Block, Program НЕ открывает собственную область видимости:
            // глобальная область уже создана один раз в конструкторе VM и должна
            // жить до конца программы, иначе объявленные здесь переменные исчезают
            // до старта "Начало" (см. §3 SEMANTICS.md).
            for (uint32_t i = 0; i < node.children_count; ++i) {
                execute(child_indices[node.children_offset + i]);
                if (should_return || should_break || should_continue) break;
            }
            break;
        }
        case Parser::NodeType::Block: {
            env.enter_scope();
            for (uint32_t i = 0; i < node.children_count; ++i) {
                execute(child_indices[node.children_offset + i]);
                if (should_return || should_break || should_continue) break; // Прерываем блок
            }
            env.exit_scope();
            break;
        }
        case Parser::NodeType::NamespaceDecl: {
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
            if (child_indices[node.children_offset + 1] != Parser::InvalidNode || !functions.count(name)) {
                functions[name] = id;
            }
            break;
        }
        case Parser::NodeType::Return: {
            Value ret_val;
            if (node.children_count > 0) {
                ret_val = eval(child_indices[node.children_offset]);
            } else {
                ret_val = Value();
            }
            should_return = true;
            return_value = ret_val;
            break;
        }
        case Parser::NodeType::Break: should_break = true; break;
        case Parser::NodeType::Continue: should_continue = true; break;
        case Parser::NodeType::Call: {
            eval(id);
            break;
        }
        case Parser::NodeType::AssignStmt: {
            eval(id);
            break;
        }
  case Parser::NodeType::StructDecl:
  case Parser::NodeType::TypeAlias: {
      break; // Ничего не делаем в рантайме
  }
    case Parser::NodeType::VarDecl: {
        Value val = eval(child_indices[node.children_offset]);
        env.define(node.token.data, val);
        break;
    }
        case Parser::NodeType::ExprStmt: {
            eval(child_indices[node.children_offset]);
            break;
        }
        case Parser::NodeType::If: {
            Value cond = eval(child_indices[node.children_offset]);
            if (cond.as.b) {
                execute(child_indices[node.children_offset + 1]);
            } else if (node.children_count > 2 && child_indices[node.children_offset + 2] != Parser::InvalidNode) {
                execute(child_indices[node.children_offset + 2]);
            }
            break;
        }
        case Parser::NodeType::While: {
            while (true) {
                Value cond = eval(child_indices[node.children_offset]);
                if (!cond.as.b) break;
                
                execute(child_indices[node.children_offset + 1]);
                
                if (should_break) { should_break = false; break; }
                if (should_continue) { should_continue = false; continue; }
                if (should_return) break;
            }
            break;
        }
  default:
    eval(id);
    break;
  }
}

int VM::run(Parser::NodeId root) {
    execute(root); // Регистрация функций
    
    Lexer::IdentId main_id = pool.intern("Начало");
    if (!functions.count(main_id)) panic("Точка входа 'Начало' не найдена");
    
    Parser::NodeId main_node = functions[main_id];
    call_stack.push_back({"Начало", nodes[main_node].token.line});
    execute(child_indices[nodes[main_node].children_offset + 1]); // Выполняем тело main
    if (!call_stack.empty()) call_stack.pop_back();
    
    if (return_value.kind == Semantic::TypeKind::Int) return return_value.as.i64;
    return 0;
}

} // namespace Interpreter