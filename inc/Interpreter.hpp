#pragma once

#include "Memory.hpp"
#include "Semantic.hpp"
#include <string>

namespace Interpreter {

// Значение в рантайме
struct Value {
  Semantic::TypeKind kind;
  // Число элементов для Array (и число полей для Struct). Заполняется в месте
  // создания значения (ArrayLiteral/StructLiteral) и далее просто копируется
  // вместе со значением (как часть "толстого указателя"), поэтому не требует
  // отдельного сопровождения при передаче в функции/присваивании. Используется
  // для проверки границ индекса (§9) и поэлементного сравнения ==/!= (§5.1).
  uint32_t count = 0;
  union {
    int64_t i64;
    double f64;
    bool b;
    const char *s; // Строки хранятся в Арене
    Value* arr; // Для массивов
  } as;

  Value() : kind(Semantic::TypeKind::Void) { as.i64 = 0; }
};

class Environment {
public:
  void define(Lexer::IdentId name, Value val);
  void assign(Lexer::IdentId name, Value val);
  Value get(Lexer::IdentId name);

  void enter_scope();
  void exit_scope();
  std::vector<std::unordered_map<Lexer::IdentId, Value>> get_scopes() const { return scopes; }
  void set_scopes(const std::vector<std::unordered_map<Lexer::IdentId, Value>>& s) { scopes = s; }

private:
  std::vector<std::unordered_map<Lexer::IdentId, Value>> scopes;
};

class VM {
public:
  VM(const std::vector<Parser::ASTNode> &nodes,
     const std::vector<Parser::NodeId> &child_indices, Lexer::StringPool &pool,
     Memory::Arena &runtime_arena);

  int run(Parser::NodeId root);

private:
  const std::vector<Parser::ASTNode> &nodes;
  const std::vector<Parser::NodeId> &child_indices;
  Lexer::StringPool &pool;
  Memory::Arena &arena; // Арена для рантайм-данных (строк)

  Environment env;
  bool should_return = false;
  bool should_break = false;
  bool should_continue = false;
  Value return_value;
  std::unordered_map<Lexer::IdentId, Parser::NodeId> functions;
  std::string namespace_prefix = "";

  struct CallFrame {
    std::string func_name;
    int call_line;
  };
  std::vector<CallFrame> call_stack;
  int current_line = 0;

  void update_line(Parser::NodeId id);

  Value eval(Parser::NodeId id);
  void execute(Parser::NodeId id);

  // Встроенные функции
  void builtin_print(const std::vector<Value> &args);

  bool values_equal(const Value &a, const Value &b);
  void check_index_bounds(const Value &arr_val, const Value &idx_val);

  void panic(const std::string &msg);
};

} // namespace Interpreter