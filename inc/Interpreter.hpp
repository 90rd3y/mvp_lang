#pragma once

#include "Memory.hpp"
#include "Semantic.hpp"
#include <string>


namespace Interpreter {

// Значение в рантайме
struct Value {
  Semantic::TypeKind kind;
  union {
    int64_t i64;
    uint64_t u64;
    double f64;
    bool b;
    const char *s; // Строки хранятся в Арене
  } as;

  Value() : kind(Semantic::TypeKind::Void) {}
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

  Value eval(Parser::NodeId id);
  void execute(Parser::NodeId id);

  // Встроенные функции
  void builtin_print(const std::vector<Value> &args);

  void panic(const std::string &msg);
};

} // namespace Interpreter