#pragma once

#include "Parser.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace Semantic {

using TypeId = uint32_t;

// --- Таблица типов ---

enum class TypeKind {
  Void,
  Int,
  Float,
  Bool,
  Char,
  String,
  Array,
  Struct,
  Alias,
  Error
};

struct Type {
  TypeKind kind;
  Lexer::IdentId name_id;
  uint32_t size;    // Для массивов или структур
  TypeId base_type; // Для массивов или псевдонимов
};

class TypeTable {
public:
  TypeTable();
  TypeId get_builtin(TypeKind kind) const;
  TypeId register_type(Type type);
  const Type &get_type(TypeId id) const { return types.at(id); }
  TypeId find_by_name(Lexer::IdentId name_id) const;

private:
  std::vector<Type> types;
  std::unordered_map<Lexer::IdentId, TypeId> name_to_id;
};

// --- Таблица символов ---

struct Symbol {
  TypeId type_id;
  bool is_mutable;
  bool is_initialized;
};

class SymbolTable {
public:
  void enter_scope();
  void exit_scope();
  bool declare(Lexer::IdentId name, Symbol sym);
  Symbol *lookup(Lexer::IdentId name);

private:
  std::vector<std::unordered_map<Lexer::IdentId, Symbol>> scopes;
};

// --- Анализатор ---

class Analyzer {
public:
  Analyzer(std::vector<Parser::ASTNode> &nodes,
           const std::vector<Parser::NodeId> &child_indices,
           Lexer::StringPool &pool);

  void analyze(Parser::NodeId root);

  // Получить тип узла после анализа
  TypeId get_node_type(Parser::NodeId node) const {
    return node_resolved_types.at(node);
  }

private:
  // Входные данные от парсера
  std::vector<Parser::ASTNode> &nodes;
  const std::vector<Parser::NodeId> &child_indices;
  Lexer::StringPool &pool;

  // Таблицы типов и символов
  TypeTable type_table;
  SymbolTable symbol_table;
  std::vector<TypeId> node_resolved_types; // Аннотация AST типами

  // Таблица структур
  struct StructField { TypeId type; uint32_t offset; };
  std::unordered_map<TypeId, std::unordered_map<Lexer::IdentId, StructField>> struct_fields;
  std::unordered_map<TypeId, uint32_t> struct_sizes;

  // Таблица функций
  struct FuncSignature {
      TypeId return_type;
      std::vector<TypeId> param_types;
      bool is_defined = false;
  };
  std::unordered_map<Lexer::IdentId, FuncSignature> functions;

  // Вспомогательные данные для анализа
  TypeId current_func_return_type = 0;
  int loop_depth = 0;
  std::string namespace_prefix = "";

  TypeId parse_type_token(Lexer::Token tok);
  TypeId check(Parser::NodeId id);

  // Вспомогательные методы проверки
  TypeId check_binary(Parser::NodeId id);
  TypeId check_unary(Parser::NodeId id);
  TypeId check_literal(Parser::NodeId id);
  TypeId check_identifier(Parser::NodeId id);
  void check_lvalue_mutability(Parser::NodeId id, Lexer::Token token);

  TypeId resolve_alias(TypeId id);
  bool types_compatible(TypeId t1, TypeId t2);
  bool is_allowed_array_base_type(TypeId id);
  bool is_castable_type(TypeId id);
  bool all_paths_return(Parser::NodeId id);

  void error(Lexer::Token token, const std::string &message);
};

} // namespace Semantic