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
           k == TypeKind::Char || k == TypeKind::String;
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

  case Parser::NodeType::Identifier:
    result = check_identifier(id);
    break;
  case Parser::NodeType::BinaryOp:
    result = check_binary(id);
    break;
  case Parser::NodeType::AssignStmt: {
      Parser::NodeId target_id = child_indices[node.children_offset];
      Parser::NodeId val_id = child_indices[node.children_offset + 1];
      
      check_lvalue_mutability(target_id, node.token);
      
      TypeId target_type = check(target_id);
      TypeId val_type = check(val_id);
      
      if (resolve_alias(target_type) != resolve_alias(val_type)) error(node.token, "Несоответствие типов при присваивании");
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
          // Это массив: проверяем, что базовый тип допустим (только примитив или строка)
          if (!is_allowed_array_base_type(base_type)) {
              error(nodes[type_node_id].token, "Базовым типом массива может быть только примитив или строка");
          }
          // Регистрируем новый тип массива. Размер (число элементов) уже лежит в extra_data.
          final_type = type_table.register_type({TypeKind::Array, 0, node.extra_data, base_type});
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
      TypeId base = parse_type_token(nodes[child_indices[node.children_offset]].token);
      if (base == type_table.get_builtin(TypeKind::Void)) error(nodes[child_indices[node.children_offset]].token, "Нельзя создать псевдоним для типа 'пусто' (void)");
      type_table.register_type({TypeKind::Alias, node.token.data, 0, base});
      result = type_table.get_builtin(TypeKind::Void);
      break;
  }
  case Parser::NodeType::StructDecl: {
      TypeId struct_id = type_table.register_type({TypeKind::Struct, node.token.data, 0, 0});
      uint32_t offset = 0;
      for (uint32_t i = 0; i < node.children_count; i += 2) {
          TypeId field_type = parse_type_token(nodes[child_indices[node.children_offset + i]].token);
          if (field_type == type_table.get_builtin(TypeKind::Void)) error(nodes[child_indices[node.children_offset + i]].token, "Поле структуры не может иметь тип 'пусто'");
          Lexer::IdentId field_name = nodes[child_indices[node.children_offset + i + 1]].token.data;
          TypeId resolved_field_type = resolve_alias(field_type);
          if (resolved_field_type == struct_id) error(nodes[child_indices[node.children_offset + i]].token, "Рекурсивное объявление структуры");
          
          struct_fields[struct_id][field_name] = {resolved_field_type, offset};
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
        error(node.token, "Элементами массива могут быть только примитивные типы или строки");
    }
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
    sig.return_type = parse_type_token(nodes[child_indices[node.children_offset]].token);
    for (uint32_t i = 2; i < node.children_count; i += 2) {
        TypeId param_type = parse_type_token(nodes[child_indices[node.children_offset + i]].token);
        if (param_type == type_table.get_builtin(TypeKind::Void)) error(nodes[child_indices[node.children_offset + i]].token, "Параметр функции не может иметь тип 'пусто'");
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