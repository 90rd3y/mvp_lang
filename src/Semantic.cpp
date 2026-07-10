#include "../inc/Semantic.hpp"
#include <iostream>

namespace Semantic {

// --- TypeTable ---

TypeTable::TypeTable() {
  // Регистрация встроенных типов (порядок важен)
  register_type({TypeKind::Void, 0, 0, 0});   // ID 0
  register_type({TypeKind::Int, 0, 8, 0});    // ID 1
  register_type({TypeKind::Uint, 0, 8, 0});   // ID 2
  register_type({TypeKind::Float, 0, 8, 0});  // ID 3
  register_type({TypeKind::Bool, 0, 1, 0});   // ID 4
  register_type({TypeKind::Char, 0, 4, 0});   // ID 5
  register_type({TypeKind::String, 0, 0, 0}); // ID 6
}

TypeId TypeTable::register_type(Type type) {
  TypeId id = static_cast<TypeId>(types.size());
  if (type.name_id != 0)
    name_to_id[type.name_id] = id;
  types.push_back(type);
  return id;
}

TypeId TypeTable::get_builtin(TypeKind kind) const {
  for (size_t i = 0; i < types.size(); ++i) {
    if (types[i].kind == kind)
      return static_cast<TypeId>(i);
  }
  return 0;
}

// --- SymbolTable ---

void SymbolTable::enter_scope() { scopes.push_back({}); }
void SymbolTable::exit_scope() { scopes.pop_back(); }

bool SymbolTable::declare(Lexer::IdentId name, Symbol sym) {
  if (scopes.back().count(name))
    return false;
  scopes.back()[name] = sym;
  return true;
}

Symbol *SymbolTable::lookup(Lexer::IdentId name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    if (it->count(name))
      return &((*it)[name]);
  }
  return nullptr;
}

// --- Analyzer ---

Analyzer::Analyzer(const std::vector<Parser::ASTNode> &n,
                   const std::vector<Parser::NodeId> &c, Lexer::StringPool &p)
    : nodes(n), child_indices(c), pool(p) {
  node_resolved_types.resize(nodes.size(), 0);
  symbol_table.enter_scope(); // Глобальная область
}

void Analyzer::error(Lexer::Token token, const std::string &message) {
  std::cerr << token.line << ":" << token.column
            << ": семантическая ошибка: " << message << std::endl;
  std::exit(1);
}

TypeId Analyzer::check(Parser::NodeId id) {
  if (id == Parser::InvalidNode)
    return 0;
  const auto &node = nodes[id];
  TypeId result = 0;

  switch (node.type) {
  case Parser::NodeType::LiteralInt:
    result = type_table.get_builtin(TypeKind::Int);
    break;
  case Parser::NodeType::LiteralFloat:
    result = type_table.get_builtin(TypeKind::Float);
    break;
  case Parser::NodeType::LiteralBool:
    result = type_table.get_builtin(TypeKind::Bool);
    break;
  case Parser::NodeType::LiteralString:
    result = type_table.get_builtin(TypeKind::String);
    break;

  case Parser::NodeType::Identifier:
    result = check_identifier(id);
    break;
  case Parser::NodeType::BinaryOp:
    result = check_binary(id);
    break;
  case Parser::NodeType::Assign:
    result = check_assignment(id);
    break;
  case Parser::NodeType::VarDecl:
    result = check_var_decl(id);
    break;

  case Parser::NodeType::Block: {
    symbol_table.enter_scope();
    for (uint32_t i = 0; i < node.children_count; ++i) {
      check(child_indices[node.children_offset + i]);
    }
    symbol_table.exit_scope();
    result = type_table.get_builtin(TypeKind::Void);
    break;
  }

  case Parser::NodeType::Program: {
    for (uint32_t i = 0; i < node.children_count; ++i) {
      check(child_indices[node.children_offset + i]);
    }
    result = type_table.get_builtin(TypeKind::Void);
    break;
  }

        case Parser::NodeType::ExprStmt: {
            result = check(child_indices[node.children_offset]);
            break;
        }
        case Parser::NodeType::If: {
            TypeId cond_type = check(child_indices[node.children_offset]);
            if (cond_type != type_table.get_builtin(TypeKind::Bool)) {
                error(node.token, "Условие 'если' должно иметь логический тип");
            }
            check(child_indices[node.children_offset + 1]); // Проверяем ветку then
            if (node.children_count > 2 && child_indices[node.children_offset + 2] != Parser::InvalidNode) {
                check(child_indices[node.children_offset + 2]); // Проверяем ветку else
            }
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
        case Parser::NodeType::While: {
            TypeId cond_type = check(child_indices[node.children_offset]);
            if (cond_type != type_table.get_builtin(TypeKind::Bool)) {
                error(node.token, "Условие 'пока' должно иметь логический тип");
            }
            check(child_indices[node.children_offset + 1]); // Проверяем тело цикла
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
  default:
    break;
  }

  node_resolved_types[id] = result;
  return result;
}

TypeId Analyzer::check_identifier(Parser::NodeId id) {
  const auto &node = nodes[id];
  Symbol *sym = symbol_table.lookup(node.token.data);
  if (!sym) {
    error(node.token, "Использование необъявленного идентификатора '" +
                          std::string(node.token.lexeme) + "'");
  }
  return sym->type_id;
}

TypeId Analyzer::check_binary(Parser::NodeId id) {
  const auto &node = nodes[id];
  TypeId left = check(child_indices[node.children_offset]);
  TypeId right = check(child_indices[node.children_offset + 1]);

  // В нашем языке строгая типизация: типы должны совпадать
  if (left != right) {
    error(node.token, "Несоответствие типов в бинарной операции");
  }

  // Для сравнений возвращаем bool, для арифметики — тип операндов
  if (node.token.type == Lexer::TokenType::EqualEqual ||
      node.token.type == Lexer::TokenType::Less) {
    return type_table.get_builtin(TypeKind::Bool);
  }
  return left;
}

TypeId Analyzer::check_var_decl(Parser::NodeId id) {
  const auto &node = nodes[id];
  // В учебной реализации тип берется из инициализатора (автовывод)
  // Или можно добавить парсинг токена типа
  Parser::NodeId init_id = child_indices[node.children_offset];
  TypeId type_id = check(init_id);

  if (!symbol_table.declare(node.token.data, {type_id, true, true})) {
    error(node.token, "Повторное объявление переменной '" +
                          std::string(node.token.lexeme) + "'");
  }
  return type_id;
}

TypeId Analyzer::check_assignment(Parser::NodeId id) {
  const auto &node = nodes[id];
  Symbol *sym = symbol_table.lookup(node.token.data);
  if (!sym)
    error(node.token, "Присваивание необъявленной переменной");

  TypeId val_type = check(child_indices[node.children_offset]);
  if (sym->type_id != val_type) {
    error(node.token, "Тип значения не совпадает с типом переменной");
  }
  return val_type;
}

void Analyzer::analyze(Parser::NodeId root) { check(root); }

} // namespace Semantic