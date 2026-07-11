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

Analyzer::Analyzer(std::vector<Parser::ASTNode> &n,
                   const std::vector<Parser::NodeId> &c, Lexer::StringPool &p)
    : nodes(n), child_indices(c), pool(p) {
  node_resolved_types.resize(nodes.size(), 0);
  symbol_table.enter_scope(); // Глобальная область
}

void Analyzer::error(Lexer::Token token, const std::string &message) {
  std::cerr << token.line << ":" << token.column
            << ": статическая ошибка: " << message << std::endl;
  std::exit(1);
}


TypeId Analyzer::resolve_alias(TypeId id) {
    while (type_table.get_type(id).kind == TypeKind::Alias) {
        id = type_table.get_type(id).base_type;
    }
    return id;
}

bool Analyzer::types_compatible(TypeId t1, TypeId t2) {
    TypeId r1 = resolve_alias(t1);
    TypeId r2 = resolve_alias(t2);
    if (r1 == r2) return true;
    auto info1 = type_table.get_type(r1);
    auto info2 = type_table.get_type(r2);
    if (info1.kind == TypeKind::Array && info2.kind == TypeKind::Array) {
        return info1.size == info2.size && resolve_alias(info1.base_type) == resolve_alias(info2.base_type);
    }
    return false;
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
  case Parser::NodeType::AssignStmt: {
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
      
      if (node.extra_data == 0) {
          TypeId check_type = final_type;
          if (type_table.get_type(check_type).kind == TypeKind::Alias) {
              check_type = type_table.get_type(check_type).base_type;
          }
          if (type_table.get_type(check_type).kind == TypeKind::Struct) {
              nodes[id].extra_data = struct_sizes[check_type];
          }
      }

      if (init_id != Parser::InvalidNode) {
          TypeId init_type = check(init_id);
          if (!types_compatible(init_type, final_type)) error(node.token, "Тип инициализатора не совпадает с типом переменной");
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
  case Parser::NodeType::StructDecl: {
      TypeId struct_id = type_table.register_type({TypeKind::Struct, node.token.data, 0, 0});
      uint32_t offset = 0;
      for (uint32_t i = 0; i < node.children_count; i += 2) {
          TypeId field_type = parse_type_token(nodes[child_indices[node.children_offset + i]].token);
          Lexer::IdentId field_name = nodes[child_indices[node.children_offset + i + 1]].token.data;
          if (field_type == struct_id) error(nodes[child_indices[node.children_offset + i]].token, "Рекурсивное объявление структуры");
          
          struct_fields[struct_id][field_name] = {field_type, offset};
          offset += 1;
      }
      struct_sizes[struct_id] = offset;
      result = type_table.get_builtin(TypeKind::Void);
      break;
  }
  case Parser::NodeType::MemberAccess: {
      TypeId obj_type = check(child_indices[node.children_offset]);
      
      if (type_table.get_type(obj_type).kind == TypeKind::Alias) {
          obj_type = type_table.get_type(obj_type).base_type;
      }
      
      if (type_table.get_type(obj_type).kind != TypeKind::Struct) {
          error(node.token, "Обращение к полю не структуры");
      }
      
      Lexer::IdentId field_name = node.token.data;
      if (!struct_fields[obj_type].count(field_name)) {
          error(node.token, "Неизвестное поле структуры");
      }
      
      auto field_info = struct_fields[obj_type][field_name];
      TypeId resolve = field_info.type;
      while (type_table.get_type(resolve).kind == TypeKind::Alias) resolve = type_table.get_type(resolve).base_type;
      bool is_struct = (type_table.get_type(resolve).kind == TypeKind::Struct);
      
      nodes[id].extra_data = field_info.offset;
      result = field_info.type;
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

        case Parser::NodeType::ArrayLiteral: {
            if (node.children_count == 0) error(node.token, "Пустые массивы не поддерживаются");
            TypeId base_type = check(child_indices[node.children_offset]);
            for (uint32_t i = 1; i < node.children_count; ++i) {
                if (!types_compatible(check(child_indices[node.children_offset + i]), base_type)) {
                    error(node.token, "Элементы массива должны быть одного типа");
                }
            }
            result = type_table.register_type({TypeKind::Array, 0, node.children_count, base_type});
            break;
        }
        case Parser::NodeType::StructLiteral: {
            Lexer::IdentId struct_name = nodes[child_indices[node.children_offset]].token.data;
            TypeId struct_id = type_table.find_by_name(struct_name);
            if (struct_id == 0) error(node.token, "Неизвестная структура для инициализации");
            
            // Упрощенная проверка количества полей
            if (node.children_count - 1 != struct_fields[struct_id].size()) {
                error(node.token, "Неверное количество полей при инициализации структуры");
            }
            result = struct_id;
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
        
        case Parser::NodeType::NamespaceDecl: {
            std::string old_prefix = namespace_prefix;
            namespace_prefix += std::string(pool.get(node.token.data)) + "::";
            for (uint32_t i = 0; i < node.children_count; ++i) {
                check(child_indices[node.children_offset + i]);
            }
            namespace_prefix = old_prefix;
            result = type_table.get_builtin(TypeKind::Void);
            break;
        }
        case Parser::NodeType::FuncDecl: {
            FuncSignature sig;
            sig.return_type = parse_type_token(nodes[child_indices[node.children_offset]].token);
            
            for (uint32_t i = 2; i < node.children_count; i += 2) {
                sig.param_types.push_back(parse_type_token(nodes[child_indices[node.children_offset + i]].token));
            }
            functions[pool.intern(namespace_prefix + std::string(pool.get(node.token.data)))] = sig;
            
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
            std::string fname_str = std::string(pool.get(func_name));

            if (fname_str == "печать" || fname_str == "ввод" || fname_str == "выход" || fname_str == "паника" || fname_str == "утверждение") {
                for (uint32_t i = 1; i < node.children_count; ++i) {
                    check(child_indices[node.children_offset + i]);
                }
                if (fname_str == "ввод") result = type_table.get_builtin(TypeKind::String);
                else result = type_table.get_builtin(TypeKind::Void);
                break;
            }

            if (!functions.count(func_name)) error(node.token, "Вызов неизвестной функции");
            
            auto& sig = functions[func_name];
            if (node.children_count - 1 != sig.param_types.size()) {
                error(node.token, "Неверное количество аргументов");
            }
            for (uint32_t i = 1; i < node.children_count; ++i) {
                TypeId arg_type = check(child_indices[node.children_offset + i]);
                if (!types_compatible(arg_type, sig.param_types[i - 1])) error(node.token, "Несоответствие типа аргумента");
            }
            result = sig.return_type;
            break;
        }
        case Parser::NodeType::Cast: {
            TypeId target_type = parse_type_token(nodes[child_indices[node.children_offset]].token);
            TypeId expr_type = check(child_indices[node.children_offset + 1]);
            if (target_type == 0) error(node.token, "Неизвестный тип для приведения");
            result = target_type;
            break;
        }
        case Parser::NodeType::Return: {
            TypeId ret_type = type_table.get_builtin(TypeKind::Void);
            if (node.children_count > 0) {
                ret_type = check(child_indices[node.children_offset]);
            }
            if (!types_compatible(ret_type, current_func_return_type)) {
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
    const auto& node = nodes[id];
    TypeId left = check(child_indices[node.children_offset]);
    TypeId right = check(child_indices[node.children_offset + 1]);

    auto resolve_alias = [&](TypeId id) {
        while (type_table.get_type(id).kind == TypeKind::Alias) {
            id = type_table.get_type(id).base_type;
        }
        return id;
    };

    if (resolve_alias(left) != resolve_alias(right)) {
        error(node.token, "Несоответствие типов в бинарной операции");
    }

    Lexer::TokenType op = node.token.type;
    
    // Проверка операторов отношения (<, >, <=, >=)
    if (op == Lexer::TokenType::Less || op == Lexer::TokenType::LessEqual ||
        op == Lexer::TokenType::Greater || op == Lexer::TokenType::GreaterEqual) {
        
        Semantic::TypeKind k = type_table.get_type(resolve_alias(left)).kind;
        if (k != TypeKind::Int && k != TypeKind::Float && k != TypeKind::Char) {
            error(node.token, "Операторы отношения (<, >, <=, >=) применимы только к числам и символам");
        }
        return type_table.get_builtin(TypeKind::Bool);
    }

    // Проверка операторов равенства (==, !=)
    if (op == Lexer::TokenType::EqualEqual || op == Lexer::TokenType::BangEqual) {
        return type_table.get_builtin(TypeKind::Bool);
    }

    // Для арифметики возвращаем тип операндов
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