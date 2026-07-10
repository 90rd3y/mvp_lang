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
    }
    env.exit_scope();
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
  execute(root);
  return 0;
}

} // namespace Interpreter