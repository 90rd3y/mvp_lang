#include "../inc/Interpreter.hpp"
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
  case Parser::NodeType::Assign: {
    Value val = eval(child_indices[node.children_offset]);
    env.assign(node.token.data, val);
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
  default:
    return Value();
  }
}

void VM::execute(Parser::NodeId id) {
  if (id == Parser::InvalidNode || should_return)
    return;
  const auto &node = nodes[id];

  switch (node.type) {
        case Parser::NodeType::Program:
        case Parser::NodeType::Block: {
            env.enter_scope();
            for (uint32_t i = 0; i < node.children_count; ++i) {
                execute(child_indices[node.children_offset + i]);
                if (should_return) break; // Прерываем выполнение блока при return
            }
            env.exit_scope();
            break;
        }
        case Parser::NodeType::FuncDecl: {
            functions[node.token.data] = id;
            break;
        }
        case Parser::NodeType::Return: {
            should_return = true;
            if (node.children_count > 0) {
                return_value = eval(child_indices[node.children_offset]);
            } else {
                return_value = Value();
            }
            break;
        }
  case Parser::NodeType::VarDecl: {
    Value val = eval(child_indices[node.children_offset]);
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
                if (!cond.as.b) break; // Выходим, если ложь
                execute(child_indices[node.children_offset + 1]);
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