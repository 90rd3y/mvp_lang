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

TypeId TypeTable::find_by_name(Lexer::IdentId name_id) const {
    auto it = name_to_id.find(name_id);
    if (it != name_to_id.end()) {
        return it->second;
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
  case Parser::NodeType::Assign: {
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
      
      if (resolve_alias(target_type) != resolve_alias(val_type)) error(node.token, "Несоответствие типов при присваивании");
      result = val_type;
      break;
  }
  case Parser::NodeType::VarDecl: {
      Parser::NodeId init_id = child_indices[node.children_offset];
      Parser::NodeId type_node_id = child_indices[node.children_offset + 1];
      
      TypeId base_type = parse_type_token(nodes[type_node_id].token);
      TypeId final_type = base_type;
      
      if (node.extra_data > 0) { // Это массив
          final_type = type_table.register_type({TypeKind::Array, 0, node.extra_data, base_type});
      }
      
      auto resolve_alias = [&](TypeId id) {
          while (type_table.get_type(id).kind == TypeKind::Alias) {
              id = type_table.get_type(id).base_type;
          }
          return id;
      };
      
      if (init_id != Parser::InvalidNode) {
          TypeId init_type = check(init_id);
          if (resolve_alias(init_type) != resolve_alias(final_type)) error(node.token, "Тип инициализатора не совпадает с типом переменной");
      }
      
      if (!symbol_table.declare(node.token.data, {final_type, true, true})) {
          error(node.token, "Повторное объявление переменной");
      }
      result = final_type;
      break;
  }
  case Parser::NodeType::TypeAlias: {
      TypeId base = parse_type_token(nodes[child_indices[node.children_offset]].token);
      type_table.register_type({TypeKind::Alias, node.token.data, 0, base});
      result = type_table.get_builtin(TypeKind::Void);
      break;
  }
  case Parser::NodeType::Indexing: {
      TypeId arr_type = check(child_indices[node.children_offset]);
      TypeId idx_type = check(child_indices[node.children_offset + 1]);
      
      auto resolve_alias = [&](TypeId id) {
          while (type_table.get_type(id).kind == TypeKind::Alias) {
              id = type_table.get_type(id).base_type;
          }
          return id;
      };
      
      if (resolve_alias(idx_type) != type_table.get_builtin(TypeKind::Int)) error(node.token, "Индекс должен быть целым числом");
      
      TypeId resolved_arr_type = resolve_alias(arr_type);
      const Type& t = type_table.get_type(resolved_arr_type);
      if (t.kind != TypeKind::Array) error(node.token, "Индексация применима только к массивам");
      
      result = t.base_type;
      break;
  }
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
            loop_depth++;
            check(child_indices[node.children_offset + 1]); // Проверяем тело цикла
            loop_depth--;
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
        case Parser::NodeType::Break:
        case Parser::NodeType::Continue: {
            if (loop_depth == 0) error(node.token, "Инструкция управления циклом вне цикла");
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
        case Parser::NodeType::BuiltinExit: {
            if (check(child_indices[node.children_offset]) != type_table.get_builtin(TypeKind::Int))
                error(node.token, "выход() ожидает целое число");
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
        case Parser::NodeType::BuiltinPanic: {
            if (check(child_indices[node.children_offset]) != type_table.get_builtin(TypeKind::String))
                error(node.token, "паника() ожидает строку");
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
        case Parser::NodeType::BuiltinAssert: {
            if (check(child_indices[node.children_offset]) != type_table.get_builtin(TypeKind::Bool))
                error(node.token, "утверждение() ожидает логическое значение");
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
        case Parser::NodeType::BuiltinInput: {
            result = type_table.get_builtin(TypeKind::String);
            break;
        }
        case Parser::NodeType::FuncDecl: {
            FuncSignature sig;
            sig.return_type = parse_type_token(nodes[child_indices[node.children_offset]].token);
            
            for (uint32_t i = 2; i < node.children_count; i += 2) {
                sig.param_types.push_back(parse_type_token(nodes[child_indices[node.children_offset + i]].token));
            }
            functions[node.token.data] = sig;
            
            symbol_table.enter_scope();
            for (uint32_t i = 2; i < node.children_count; i += 2) {
                Lexer::IdentId arg_name = nodes[child_indices[node.children_offset + i + 1]].token.data;
                symbol_table.declare(arg_name, {sig.param_types[(i-2)/2], true, true});
            }
            
            TypeId old_ret = current_func_return_type;
            current_func_return_type = sig.return_type;
            check(child_indices[node.children_offset + 1]); // Проверяем тело
            current_func_return_type = old_ret;
            
            symbol_table.exit_scope();
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
        case Parser::NodeType::Call: {
            Parser::NodeId callee_id = child_indices[node.children_offset];
            Lexer::IdentId func_name = nodes[callee_id].token.data;
            if (!functions.count(func_name)) error(node.token, "Вызов неизвестной функции");
            
            auto& sig = functions[func_name];
            if (node.children_count - 1 != sig.param_types.size()) {
                error(node.token, "Неверное количество аргументов");
            }
            for (uint32_t i = 1; i < node.children_count; ++i) {
                TypeId arg_type = check(child_indices[node.children_offset + i]);
                if (arg_type != sig.param_types[i - 1]) error(node.token, "Несоответствие типа аргумента");
            }
            result = sig.return_type;
            break;
        }
        case Parser::NodeType::Return: {
            TypeId ret_type = type_table.get_builtin(TypeKind::Void);
            if (node.children_count > 0) {
                ret_type = check(child_indices[node.children_offset]);
            }
            if (ret_type != current_func_return_type) {
                error(node.token, "Тип возвращаемого значения не совпадает с сигнатурой функции");
            }
            result = ret_type;
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
  auto resolve_alias = [&](TypeId id) {
      while (type_table.get_type(id).kind == TypeKind::Alias) {
          id = type_table.get_type(id).base_type;
      }
      return id;
  };
  
  if (resolve_alias(left) != resolve_alias(right)) {
    error(node.token, "Несоответствие типов в бинарной операции");
  }

  // Для сравнений возвращаем bool, для арифметики — тип операндов
  if (node.token.type == Lexer::TokenType::EqualEqual ||
      node.token.type == Lexer::TokenType::Less) {
    return type_table.get_builtin(TypeKind::Bool);
  }
  return left;
}



void Analyzer::analyze(Parser::NodeId root) { check(root); }

TypeId Analyzer::parse_type_token(Lexer::Token tok) {
    if (tok.type == Lexer::TokenType::Identifier) {
        TypeId id = type_table.find_by_name(tok.data);
        if (id == 0) error(tok, "Использование неизвестного типа");
        return id;
    }
    switch (tok.type) {
        case Lexer::TokenType::KwInt: return type_table.get_builtin(TypeKind::Int);
        case Lexer::TokenType::KwFloat: return type_table.get_builtin(TypeKind::Float);
        case Lexer::TokenType::KwBool: return type_table.get_builtin(TypeKind::Bool);
        case Lexer::TokenType::KwVoid: return type_table.get_builtin(TypeKind::Void);
        case Lexer::TokenType::KwString: return type_table.get_builtin(TypeKind::String);
        default: return 0;
    }
}

} // namespace Semantic