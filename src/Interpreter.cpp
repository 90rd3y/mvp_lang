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

void VM::panic(const std::string &msg) {
  std::cerr << "Ошибка времени выполнения: " << msg << std::endl;
  std::exit(1);
}

Value VM::eval(Parser::NodeId id) {
  if (id == Parser::InvalidNode)
    return Value();
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
    case Lexer::TokenType::EqualEqual:
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = (left.as.i64 == right.as.i64); // Упрощенно
      break;
    case Lexer::TokenType::Less:
      res.kind = Semantic::TypeKind::Bool;
      res.as.b = (left.as.i64 < right.as.i64); // Упрощенно
      break;
    default:
      break;
    }
    return res;
  }
    case Parser::NodeType::Indexing: {
        Value arr_val = eval(child_indices[node.children_offset]);
        Value idx_val = eval(child_indices[node.children_offset + 1]);
        // Упрощенно: без проверки выхода за границы для MVP
        return arr_val.as.arr[idx_val.as.i64];
    }
    case Parser::NodeType::Assign: {
        Parser::NodeId target_id = child_indices[node.children_offset];
        Value val = eval(child_indices[node.children_offset + 1]);
        
        if (nodes[target_id].type == Parser::NodeType::Identifier) {
            env.assign(nodes[target_id].token.data, val);
        } else if (nodes[target_id].type == Parser::NodeType::Indexing) {
            Value arr_val = eval(child_indices[nodes[target_id].children_offset]);
            Value idx_val = eval(child_indices[nodes[target_id].children_offset + 1]);
            arr_val.as.arr[idx_val.as.i64] = val;
        }
        return val;
    }
        case Parser::NodeType::Call: {
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
        }
        case Parser::NodeType::BuiltinInput: {
            std::string input_str;
            std::getline(std::cin, input_str);
            
            // Выделяем память под строку в runtime_arena
            char* mem = static_cast<char*>(arena.alloc(input_str.size() + 1, 1));
            std::memcpy(mem, input_str.data(), input_str.size());
            mem[input_str.size()] = '\0';
            
            Value v; v.kind = Semantic::TypeKind::String;
            v.as.s = mem;
            return v;
        }
  default:
    return Value();
  }
}

void VM::execute(Parser::NodeId id) {
  if (id == Parser::InvalidNode || should_return || should_break || should_continue)
    return;
  const auto &node = nodes[id];

  switch (node.type) {
        case Parser::NodeType::Program:
        case Parser::NodeType::Block: {
            env.enter_scope();
            for (uint32_t i = 0; i < node.children_count; ++i) {
                execute(child_indices[node.children_offset + i]);
                if (should_return || should_break || should_continue) break; // Прерываем блок
            }
            env.exit_scope();
            break;
        }
        case Parser::NodeType::FuncDecl: {
            functions[node.token.data] = id;
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
        case Parser::NodeType::BuiltinExit: {
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
        }
  case Parser::NodeType::TypeAlias: {
      break; // Ничего не делаем в рантайме
  }
  case Parser::NodeType::VarDecl: {
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
  }
        case Parser::NodeType::ExprStmt: {
            Parser::NodeId expr_id = child_indices[node.children_offset];
            Value val = eval(expr_id);
            
            // Чистая обработка функции "печать"
            if (node.token.type == Lexer::TokenType::KwPrint) {
                switch (val.kind) {
                    case Semantic::TypeKind::Int: std::cout << val.as.i64 << "\n"; break;
                    case Semantic::TypeKind::Float: std::cout << val.as.f64 << "\n"; break;
                    case Semantic::TypeKind::Bool: std::cout << (val.as.b ? "истина" : "ложь") << "\n"; break;
                    case Semantic::TypeKind::String: std::cout << val.as.s << "\n"; break;
                    default: std::cout << "void\n"; break;
                }
            }
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
    
    Lexer::IdentId main_id = pool.intern("main");
    if (!functions.count(main_id)) panic("Точка входа 'main' не найдена");
    
    Parser::NodeId main_node = functions[main_id];
    execute(child_indices[nodes[main_node].children_offset + 1]); // Выполняем тело main
    
    if (return_value.kind == Semantic::TypeKind::Int) return return_value.as.i64;
    return 0;
}

} // namespace Interpreter