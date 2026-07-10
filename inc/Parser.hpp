#pragma once

#include "Lexer.hpp"
#include <optional>
#include <vector>

namespace Parser {

using NodeId = uint32_t;
const NodeId InvalidNode = 0;

enum class NodeType {
  None,
  // Выражения
  LiteralInt,
  LiteralFloat,
  LiteralString,
  LiteralBool,
  Identifier,
  BinaryOp,
  UnaryOp,
  Assign,
  Call,
  Indexing,
  MemberAccess,

  // Инструкции
  Block,
  VarDecl,
  FuncDecl,
  StructDecl,
  TypeAlias,
  If,
  While,
  Return,
  Break,
  Continue,
  ExprStmt,
  BuiltinExit, BuiltinPanic, BuiltinAssert, BuiltinInput,

  Program
};

struct ASTNode {
  NodeType type;
  Lexer::Token token;
  uint32_t children_offset; // Индекс в массиве child_indices
  uint32_t children_count;
};

class Parser {
public:
  Parser(const std::vector<Lexer::Token> &tokens);
  NodeId parse();

  const std::vector<ASTNode> &get_nodes() const { return nodes; }
  const std::vector<NodeId> &get_child_indices() const { return child_indices; }

private:
  const std::vector<Lexer::Token> &tokens;
  size_t current = 0;

  std::vector<ASTNode> nodes;
  std::vector<NodeId> child_indices;

  // Вспомогательные методы
  NodeId create_node(NodeType type, Lexer::Token token,
                     const std::vector<NodeId> &children = {});
  Lexer::Token advance();
  Lexer::Token peek() const;
  Lexer::Token previous() const;
  bool check(Lexer::TokenType type) const;
  bool match(Lexer::TokenType type);
  Lexer::Token consume(Lexer::TokenType type, const char *message);
  void error(Lexer::Token token, const char *message);

  // Рекурсивный спуск (Инструкции и объявления)
  NodeId declaration();
  NodeId var_declaration();
  NodeId func_declaration();
  NodeId struct_declaration();
  NodeId type_alias();
  NodeId statement();
  NodeId if_statement();
  NodeId while_statement();
  NodeId block();
  NodeId return_statement();

  // Pratt Parser (Выражения)
  enum Precedence {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // ||
    PREC_AND,        // &&
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * / %
    PREC_UNARY,      // ! -
    PREC_CALL,       // . () []
    PREC_PRIMARY
  };

  NodeId expression();
  NodeId parse_precedence(Precedence precedence);

  // Таблица правил Pratt
  struct ParseRule {
    typedef NodeId (Parser::*ParseFn)(bool can_assign);
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
  };

  const ParseRule *get_rule(Lexer::TokenType type);

  NodeId grouping(bool can_assign);
  NodeId unary(bool can_assign);
  NodeId binary(bool can_assign);
  NodeId literal(bool can_assign);
  NodeId variable(bool can_assign);
  NodeId call(bool can_assign);
  NodeId builtin_input(bool can_assign);
};

} // namespace Parser