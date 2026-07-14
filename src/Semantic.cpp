#include "../inc/Semantic.hpp"
#include <iostream>

namespace Semantic {

// --- TypeTable ---

TypeTable::TypeTable() {
  // Регистрация встроенных типов (порядок важен)
  register_type({TypeKind::Void, 0, 0, 0});   // ID 0
  register_type({TypeKind::Int, 0, 8, 0});    // ID 1
  register_type({TypeKind::Float, 0, 8, 0});  // ID 2
  register_type({TypeKind::Bool, 0, 1, 0});   // ID 3
  register_type({TypeKind::Char, 0, 4, 0});   // ID 4
  register_type({TypeKind::String, 0, 0, 0}); // ID 5
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

bool Analyzer::is_allowed_array_base_type(TypeId id) {
    TypeId resolved = resolve_alias(id);
    TypeKind k = type_table.get_type(resolved).kind;
    return k == TypeKind::Int || 
           k == TypeKind::Float || k == TypeKind::Bool || 
           k == TypeKind::Char || k == TypeKind::String ||
           k == TypeKind::Struct;
}

bool Analyzer::is_castable_type(TypeId id) {
    TypeId resolved = resolve_alias(id);
    TypeKind k = type_table.get_type(resolved).kind;
    return k == TypeKind::Int || k == TypeKind::Float || k == TypeKind::Bool;
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

// Рекурсивный семантический диспетчер
TypeId Analyzer::check(Parser::NodeId id) {
  if (id == Parser::InvalidNode)
    return 0;
  const auto &node = nodes[id];
  TypeId result = 0;

  // Литералы
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
  case Parser::NodeType::LiteralChar:
    result = type_table.get_builtin(TypeKind::Char);
    break;

  case Parser::NodeType::Identifier:
    result = check_identifier(id);
    break;
  case Parser::NodeType::BinaryOp:
    result = check_binary(id);
    break;
  case Parser::NodeType::UnaryOp:
    result = check_unary(id);
    break;
  case Parser::NodeType::AssignStmt: {
      Parser::NodeId target_id = child_indices[node.children_offset];
      Parser::NodeId val_id = child_indices[node.children_offset + 1];
      
      check_lvalue_mutability(target_id, node.token);
      
      TypeId target_type = check(target_id);
      TypeId val_type = check(val_id);

      if (!types_compatible(target_type, val_type)) error(node.token, "Несоответствие типов при присваивании");
      result = type_table.get_builtin(TypeKind::Void);
      break;
  }
  case Parser::NodeType::VarDecl: {
      Parser::NodeId init_id = child_indices[node.children_offset];
      Parser::NodeId type_node_id = child_indices[node.children_offset + 1];
      
      // Выявить базовый тип переменной. 
      // extra_data используется для передачи размера массива
      TypeId base_type = parse_type_token(nodes[type_node_id].token);
      if (base_type == type_table.get_builtin(TypeKind::Void)) error(nodes[type_node_id].token, "Переменная не может иметь тип 'пусто' (void)");
      TypeId final_type = base_type;

      if (node.extra_data > 0) {
          // Это массив: размер (число элементов) уже лежит в extra_data.
          final_type = wrap_array_if_needed(base_type, node.extra_data, nodes[type_node_id].token);
      } else {
          // Это не массив. Проверяем, не является ли тип структурой.
          TypeId check_type = resolve_alias(final_type);
          if (type_table.get_type(check_type).kind == TypeKind::Struct) {
              // Для структур записываем их размер (в байтах/полях) в extra_data для генератора кода.
              // Так как пустые структуры запрещены, размер будет строго > 0.
              nodes[id].extra_data = struct_sizes[check_type];
          }
      }

      if (init_id != Parser::InvalidNode) {
          TypeId init_type = check(init_id);
          if (!types_compatible(init_type, final_type)) error(node.token, "Тип инициализатора не совпадает с типом переменной");
      }
      
      if (!symbol_table.declare(node.token.data, {final_type, node.is_mutable, true})) {
          error(node.token, "Повторное объявление переменной");
      }
      result = final_type;
      break;
  }
  case Parser::NodeType::TypeAlias: {
      Parser::NodeId base_type_node = child_indices[node.children_offset];
      TypeId base = parse_type_token(nodes[base_type_node].token);
      if (base == type_table.get_builtin(TypeKind::Void)) error(nodes[base_type_node].token, "Нельзя создать псевдоним для типа 'пусто' (void)");
      TypeId aliased_type = wrap_array_if_needed(base, nodes[base_type_node].extra_data, nodes[base_type_node].token);
      type_table.register_type({TypeKind::Alias, node.token.data, 0, aliased_type});
      result = type_table.get_builtin(TypeKind::Void);
      break;
  }
  case Parser::NodeType::StructDecl: {
      TypeId struct_id = type_table.register_type({TypeKind::Struct, node.token.data, 0, 0});
      uint32_t offset = 0;
      for (uint32_t i = 0; i < node.children_count; i += 2) {
          Parser::NodeId field_type_node = child_indices[node.children_offset + i];
          TypeId field_type = parse_type_token(nodes[field_type_node].token);
          if (field_type == type_table.get_builtin(TypeKind::Void)) error(nodes[field_type_node].token, "Поле структуры не может иметь тип 'пусто'");
          Lexer::IdentId field_name = nodes[child_indices[node.children_offset + i + 1]].token.data;

          uint32_t array_size = nodes[field_type_node].extra_data;
          TypeId stored_field_type;
          if (array_size > 0) {
              // Поле-массив (например "целое[3] поле;"): само по себе не рекурсивно,
              // так как массив хранится как указатель (см. §2 SEMANTICS.md), а не
              // как встроенное по значению поле, поэтому массив структуры на саму
              // себя (дерево/список) допустим.
              stored_field_type = wrap_array_if_needed(field_type, array_size, nodes[field_type_node].token);
          } else {
              stored_field_type = resolve_alias(field_type);
              if (stored_field_type == struct_id) error(nodes[field_type_node].token, "Рекурсивное объявление структуры");
          }

          struct_fields[struct_id][field_name] = {stored_field_type, offset};
          offset += 1;
      }
      struct_sizes[struct_id] = offset;
      result = type_table.get_builtin(TypeKind::Void);
      break;
  }
  case Parser::NodeType::MemberAccess: {
      TypeId obj_type = resolve_alias(check(child_indices[node.children_offset]));
      
      if (type_table.get_type(obj_type).kind != TypeKind::Struct) {
          error(node.token, "Обращение к полю не структуры");
      }
      
      Lexer::IdentId field_name = node.token.data;
      if (!struct_fields[obj_type].count(field_name)) {
          error(node.token, "Несуществующее поле структуры");
      }
      
      auto field_info = struct_fields[obj_type][field_name];
      nodes[id].extra_data = field_info.offset;
      result = field_info.type;
      break;
  }
  case Parser::NodeType::Indexing: {
      TypeId arr_type = check(child_indices[node.children_offset]);
      TypeId idx_type = check(child_indices[node.children_offset + 1]);
      
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
    if (!is_allowed_array_base_type(base_type)) {
        error(node.token, "Элементами массива могут быть только примитивные типы, строки или структуры");
    }
    for (uint32_t i = 1; i < node.children_count; ++i) {
        if (!types_compatible(check(child_indices[node.children_offset + i]), base_type)) {
            error(node.token, "Элементы массива должны быть одного типа");
        }
    }
    result = canonical_array_type(base_type, node.children_count);
    break;
  }
  case Parser::NodeType::StructLiteral: {
    Lexer::IdentId struct_name = nodes[child_indices[node.children_offset]].token.data;
    TypeId struct_id = type_table.find_by_name(struct_name);
    if (struct_id == 0) error(node.token, "Неизвестная структура для инициализации");
    if (node.children_count - 1 != struct_fields[struct_id].size()) {
        error(node.token, "Неверное количество полей при инициализации структуры");
    }

    std::vector<TypeId> expected_types(struct_fields[struct_id].size());
    for (const auto& kv : struct_fields[struct_id]) {
        expected_types[kv.second.offset] = kv.second.type;
    }

    for (uint32_t i = 1; i < node.children_count; ++i) {
        TypeId expr_type = check(child_indices[node.children_offset + i]);
        if (!types_compatible(expr_type, expected_types[i - 1])) {
            error(node.token, "Несоответствие типов при инициализации поля структуры");
        }
    }

    result = struct_id;
    break;
  }
  case Parser::NodeType::ExprStmt: {
    Parser::NodeId expr_id = child_indices[node.children_offset];
    if (nodes[expr_id].type != Parser::NodeType::Call) {
        error(node.token, "В качестве самостоятельной инструкции допускается только вызов функции");
    }
    check(expr_id);
    result = type_table.get_builtin(TypeKind::Void);
    break;
  }
  case Parser::NodeType::If: {
    TypeId cond_type = check(child_indices[node.children_offset]);
    if (cond_type != type_table.get_builtin(TypeKind::Bool)) error(node.token, "Условие 'если' должно иметь логический тип");
    check(child_indices[node.children_offset + 1]); // Проверяем ветку then
    if (node.children_count > 2 && child_indices[node.children_offset + 2] != Parser::InvalidNode) {
        check(child_indices[node.children_offset + 2]); // Проверяем ветку else
    }
    result = type_table.get_builtin(TypeKind::Void);
    break;
  }
  case Parser::NodeType::While: {
    TypeId cond_type = check(child_indices[node.children_offset]);
    if (cond_type != type_table.get_builtin(TypeKind::Bool)) error(node.token, "Условие 'пока' должно иметь логический тип");
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

    // Выявление типов возвращаемого и параметрических типов
    Parser::NodeId ret_type_node = child_indices[node.children_offset];
    TypeId ret_base_type = parse_type_token(nodes[ret_type_node].token);
    sig.return_type = wrap_array_if_needed(ret_base_type, nodes[ret_type_node].extra_data, nodes[ret_type_node].token);
    for (uint32_t i = 2; i < node.children_count; i += 2) {
        Parser::NodeId param_type_node = child_indices[node.children_offset + i];
        TypeId param_base_type = parse_type_token(nodes[param_type_node].token);
        if (param_base_type == type_table.get_builtin(TypeKind::Void)) error(nodes[param_type_node].token, "Параметр функции не может иметь тип 'пусто'");
        TypeId param_type = wrap_array_if_needed(param_base_type, nodes[param_type_node].extra_data, nodes[param_type_node].token);
        sig.param_types.push_back(param_type);
    }
    
    Parser::NodeId body_id = child_indices[node.children_offset + 1];
    bool is_prototype = (body_id == Parser::InvalidNode);
    sig.is_defined = !is_prototype;

    // Выявление полного имени функции и проверка на объявление
    Lexer::IdentId full_name = pool.intern(namespace_prefix + std::string(pool.get(node.token.data)));
    if (functions.count(full_name)) {
        const FuncSignature &prev = functions[full_name];
        if (prev.return_type != sig.return_type || prev.param_types != sig.param_types) {
            error(node.token, "Сигнатура функции не совпадает с предыдущим объявлением");
        }
        if (prev.is_defined && !is_prototype) {
            error(node.token, "Функция с таким именем уже объявлена");
        }
        if (!prev.is_defined && !is_prototype) {
            functions[full_name].is_defined = true;
        }
    } else {
        functions[full_name] = sig;
    }
    
    if (is_prototype) {
        result = type_table.get_builtin(TypeKind::Void);
        break;
    }
    
    symbol_table.enter_scope();
    for (uint32_t i = 2; i < node.children_count; i += 2) {
      Lexer::IdentId arg_name = nodes[child_indices[node.children_offset + i + 1]].token.data;
      symbol_table.declare(arg_name, {sig.param_types[(i-2)/2], true, true});
    }
    TypeId old_ret = current_func_return_type;
    current_func_return_type = sig.return_type;
    check(body_id); // Проверяем тело
    if (sig.return_type != type_table.get_builtin(TypeKind::Void)) {
      if (!all_paths_return(body_id)) {
        error(node.token, "Не все пути выполнения в функции гарантируют возврат значения");
      }
    }
    current_func_return_type = old_ret;
    symbol_table.exit_scope();
    result = type_table.get_builtin(TypeKind::Void);
    break;
  }
  case Parser::NodeType::Call: {
    Parser::NodeId callee_id = child_indices[node.children_offset];
    Lexer::IdentId func_name = nodes[callee_id].token.data;

    // Проверка на встроенные функции
    std::string fname_str = std::string(pool.get(func_name));
    if (fname_str == "печать" || fname_str == "ввод" || fname_str == "выход" || fname_str == "паника" || fname_str == "утверждение") {
      // У каждой встроенной функции ровно одна допустимая арность: "ввод" не использует
      // аргументы вовсе, остальные обращаются к args[0] безусловно (см. §7 SEMANTICS.md),
      // поэтому 0 или >1 аргументов запрещены статически, а не тихо игнорируются.
      uint32_t expected_args = (fname_str == "ввод") ? 0 : 1;
      uint32_t actual_args = node.children_count - 1;
      if (actual_args != expected_args) {
        error(node.token, "Встроенная функция '" + fname_str + "' требует ровно " +
                               std::to_string(expected_args) + " аргумент(ов), передано " +
                               std::to_string(actual_args));
      }
      for (uint32_t i = 1; i < node.children_count; ++i) {
        check(child_indices[node.children_offset + i]);
      }
      if (fname_str == "ввод") result = type_table.get_builtin(TypeKind::String);
      else result = type_table.get_builtin(TypeKind::Void);
      break;
    }

    // Проверка на объявленность функции
    if (!functions.count(func_name)) error(node.token, "Вызов неизвестной функции");

    // Проверка на количество аргументов
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
    if (target_type == type_table.get_builtin(TypeKind::Void)) error(node.token, "Приведение к типу 'пусто' (void) запрещено");
    if (!is_castable_type(target_type) || !is_castable_type(expr_type)) {
      error(node.token, "Допускается приведение типов только между целыми, плавающей точкой и логическим типом");
    }
    result = target_type;
    break;
  }
  case Parser::NodeType::Return: {
    TypeId ret_type = type_table.get_builtin(TypeKind::Void);
    if (node.children_count > 0) ret_type = check(child_indices[node.children_offset]);
    if (!types_compatible(ret_type, current_func_return_type)) error(node.token, "Тип возвращаемого значения не совпадает с сигнатурой функции");
    result = ret_type;
    break;
  }
  default:
    break;
  }

  node_resolved_types[id] = result;
  return result;
}

// Проверка на то, что идентификатор был объявлен и является переменной
TypeId Analyzer::check_identifier(Parser::NodeId id) {
  const auto &node = nodes[id];
  Symbol *sym = symbol_table.lookup(node.token.data);
  if (!sym) {
    error(node.token, "Использование необъявленного идентификатора '" +
                          std::string(node.token.lexeme) + "'");
  }
  return sym->type_id;
}

void Analyzer::check_lvalue_mutability(Parser::NodeId id, Lexer::Token token) {
  if (id == Parser::InvalidNode)
    return;
  const auto &node = nodes[id];
  if (node.type == Parser::NodeType::Identifier) {
    Symbol *sym = symbol_table.lookup(node.token.data);
    if (sym && !sym->is_mutable) {
      error(token, "Попытка присваивания константной (неизменяемой) переменной '" +
                       std::string(node.token.lexeme) + "'");
    }
  } else if (node.type == Parser::NodeType::Indexing ||
             node.type == Parser::NodeType::MemberAccess) {
    check_lvalue_mutability(child_indices[node.children_offset], token);
  } else {
    error(token, "Недопустимое выражение слева от оператора присваивания (ожидается переменная, элемент массива или поле структуры)");
  }
}

bool Analyzer::all_paths_return(Parser::NodeId id) {
  if (id == Parser::InvalidNode)
    return false;
  const auto &node = nodes[id];
  switch (node.type) {
  case Parser::NodeType::Return:
    return true;
  case Parser::NodeType::Block: {
    for (uint32_t i = 0; i < node.children_count; ++i) {
      if (all_paths_return(child_indices[node.children_offset + i])) {
        return true;
      }
    }
    return false;
  }
  case Parser::NodeType::If: {
    if (node.children_count < 3 || child_indices[node.children_offset + 2] == Parser::InvalidNode) {
      return false;
    }
    Parser::NodeId then_id = child_indices[node.children_offset + 1];
    Parser::NodeId else_id = child_indices[node.children_offset + 2];
    return all_paths_return(then_id) && all_paths_return(else_id);
  }
  case Parser::NodeType::ExprStmt: {
    return all_paths_return(child_indices[node.children_offset]);
  }
  case Parser::NodeType::Call: {
    Parser::NodeId callee_id = child_indices[node.children_offset];
    Lexer::IdentId func_name = nodes[callee_id].token.data;
    std::string fname_str = std::string(pool.get(func_name));
    if (fname_str == "паника" || fname_str == "выход") {
      return true;
    }
    return false;
  }
  case Parser::NodeType::While: {
    // "пока(истина) { ... }" может завершиться только через return (тогда путь
    // гарантирует значение) либо через break (тогда управление уходит после цикла,
    // и гарантии нет). Если условие не буквально "истина", цикл может завершиться
    // штатно по условию, что тоже не даёт гарантии.
    Parser::NodeId cond_id = child_indices[node.children_offset];
    const auto &cond_node = nodes[cond_id];
    bool always_true = cond_node.type == Parser::NodeType::LiteralBool &&
                        cond_node.token.type == Lexer::TokenType::KwTrue;
    if (!always_true) return false;
    Parser::NodeId body_id = child_indices[node.children_offset + 1];
    return !contains_own_break(body_id);
  }
  default:
    return false;
  }
}

// Ищет "прервать", относящийся именно к этому циклу (т.е. не поглощённый вложенным
// пока/циклом, у которого свой break). Используется только для while(true) в
// all_paths_return: если такого break нет, цикл покинуть можно только через return.
bool Analyzer::contains_own_break(Parser::NodeId id) {
  if (id == Parser::InvalidNode) return false;
  const auto &node = nodes[id];
  switch (node.type) {
  case Parser::NodeType::Break:
    return true;
  case Parser::NodeType::Block: {
    for (uint32_t i = 0; i < node.children_count; ++i) {
      if (contains_own_break(child_indices[node.children_offset + i])) return true;
    }
    return false;
  }
  case Parser::NodeType::If: {
    if (contains_own_break(child_indices[node.children_offset + 1])) return true;
    if (node.children_count > 2 && contains_own_break(child_indices[node.children_offset + 2])) return true;
    return false;
  }
  case Parser::NodeType::While:
    return false; // Вложенный цикл поглощает свои break — сюда они не относятся.
  default:
    return false;
  }
}

// Проверка допустимости бинарной операции, возвращает тип результата.
TypeId Analyzer::check_binary(Parser::NodeId id) {
    const auto& node = nodes[id];
    TypeId left = check(child_indices[node.children_offset]);
    TypeId right = check(child_indices[node.children_offset + 1]);

    if (resolve_alias(left) != resolve_alias(right)) {
        error(node.token, "Несоответствие типов в бинарной операции");
    }

    Lexer::TokenType op = node.token.type;
    Semantic::TypeKind k = type_table.get_type(resolve_alias(left)).kind;
    
    // Проверка логических операторов (&&, ||)
    if (op == Lexer::TokenType::AmpAmp || op == Lexer::TokenType::PipePipe) {
        if (k != TypeKind::Bool) {
            error(node.token, "Логические операторы (&&, ||) применимы только к типу Bool");
        }
        return type_table.get_builtin(TypeKind::Bool);
    }
    
    // Проверка операторов отношения (<, >, <=, >=)
    if (op == Lexer::TokenType::Less || op == Lexer::TokenType::LessEqual ||
        op == Lexer::TokenType::Greater || op == Lexer::TokenType::GreaterEqual) {
        if (k != TypeKind::Int && k != TypeKind::Float) {
            error(node.token, "Операторы отношения (<, >, <=, >=) применимы только к числам");
        }
        return type_table.get_builtin(TypeKind::Bool);
    }

    // Проверка операторов равенства (==, !=)
    if (op == Lexer::TokenType::EqualEqual || op == Lexer::TokenType::BangEqual) {
        return type_table.get_builtin(TypeKind::Bool);
    }

    // Проверка арифметических операторов (+, -, *, /)
    if (op == Lexer::TokenType::Plus || op == Lexer::TokenType::Minus ||
        op == Lexer::TokenType::Star || op == Lexer::TokenType::Slash) {
        if (k != TypeKind::Int && k != TypeKind::Float) {
            error(node.token, "Арифметические операторы (+, -, *, /) применимы только к числам");
        }
        return left;
    }

    // Проверка остатка от деления (%)
    if (op == Lexer::TokenType::Percent) {
        if (k != TypeKind::Int) {
            error(node.token, "Оператор остатка от деления (%) применим только к целым числам");
        }
        return left;
    }

    // Для любых других бинарных операций возвращаем тип операндов
    return left;
}

TypeId Analyzer::check_unary(Parser::NodeId id) {
    const auto& node = nodes[id];
    Parser::NodeId operand_id = child_indices[node.children_offset];
    TypeId operand_type = check(operand_id);
    if (operand_type == 0) return 0;
    
    if (node.token.type == Lexer::TokenType::Minus) {
        if (operand_type == type_table.get_builtin(TypeKind::Int) ||
            operand_type == type_table.get_builtin(TypeKind::Float)) {
            return operand_type;
        }
        error(node.token, "Оператор '-' применим только к числам");
    } else if (node.token.type == Lexer::TokenType::Bang) {
        if (operand_type == type_table.get_builtin(TypeKind::Bool)) {
            return operand_type;
        }
        error(node.token, "Оператор '!' применим только к логическим значениям");
    } else {
        error(node.token, "Неизвестный унарный оператор");
    }
    return 0;
}



void Analyzer::analyze(Parser::NodeId root) {
  check(root);

  // Точка входа "Начало" обязательна: без неё VM::run() не сможет запуститься.
  // Проверяем это здесь же, статически, а не оставляем на обнаружение в рантайме.
  Lexer::IdentId main_id = pool.intern("Начало");
  auto it = functions.find(main_id);
  if (it == functions.end() || !it->second.is_defined) {
    error(nodes[root].token, "Точка входа 'Начало' не найдена");
  }
}

// Возвращает существующий TypeId для массива "base_type[size]", если такой уже был
// зарегистрирован, иначе регистрирует новый и запоминает его. Без этой канонизации
// каждое упоминание одного и того же по структуре типа массива получало бы свой
// TypeId, и прямое сравнение TypeId == TypeId (например, при присваивании массива
// целиком или в бинарной операции) ошибочно считало бы структурно одинаковые массивы
// разными типами (см. §4 SEMANTICS.md).
TypeId Analyzer::canonical_array_type(TypeId base_type, uint32_t size) {
  TypeId resolved_base = resolve_alias(base_type);
  auto key = std::make_pair(resolved_base, size);
  auto it = array_type_cache.find(key);
  if (it != array_type_cache.end()) return it->second;
  TypeId new_id = type_table.register_type({TypeKind::Array, 0, size, resolved_base});
  array_type_cache[key] = new_id;
  return new_id;
}

// Общая точка для любого места, где тип объявляется через "БазовыйТип[N]"
// (переменная, поле структуры, параметр/возврат функции, синоним типа).
// array_size == 0 означает "это не массив" — базовый тип возвращается как есть.
TypeId Analyzer::wrap_array_if_needed(TypeId base_type, uint32_t array_size, Lexer::Token err_tok) {
  if (array_size == 0) return base_type;
  if (!is_allowed_array_base_type(base_type)) {
    error(err_tok, "Базовым типом массива может быть только примитив, строка или структура");
  }
  return canonical_array_type(base_type, array_size);
}

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
        case Lexer::TokenType::KwChar: return type_table.get_builtin(TypeKind::Char);
        case Lexer::TokenType::KwVoid: return type_table.get_builtin(TypeKind::Void);
        case Lexer::TokenType::KwString: return type_table.get_builtin(TypeKind::String);
        default: return 0;
    }
}

} // namespace Semantic