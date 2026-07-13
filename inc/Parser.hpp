#pragma once

#include "Lexer.hpp"

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
  LiteralChar,
  Identifier,
  BinaryOp,
  UnaryOp,
  Call,
  Indexing,
  MemberAccess,
  Cast,
  ArrayLiteral,
  StructLiteral,

  // Инструкции
  Block,
  VarDecl,
  FuncDecl,
  StructDecl,
  TypeAlias,
  NamespaceDecl,
  If,
  While,
  Return,
  Break,
  Continue,
  ExprStmt,
  AssignStmt,

  Program
};

struct ASTNode {
  NodeType type;
  Lexer::Token token;
  uint32_t children_offset; // Индекс в массиве child_indices
  uint32_t children_count;
  uint32_t extra_data = 0; // Сюда будем писать размер массива
  bool is_mutable = true;
};

class Parser {
public:
  Parser(const std::vector<Lexer::Token> &tokens, Lexer::StringPool &pool);
  NodeId parse();

  std::vector<ASTNode>& get_nodes() { return nodes; }
  const std::vector<NodeId> &get_child_indices() const { return child_indices; }

private:
  const std::vector<Lexer::Token> &tokens;
  Lexer::StringPool &pool;
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
  Lexer::Token parse_type_token();
  bool is_type_token(size_t index) const;

  // Рекурсивный спуск (Инструкции и объявления)
  NodeId declaration();
  NodeId var_declaration(bool is_mutable = true);
  NodeId func_declaration();
  NodeId struct_declaration();
  NodeId type_alias();
  NodeId namespace_declaration();
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
  NodeId variable(bool can_assign);
  NodeId literal(bool can_assign);
  NodeId array_literal(bool can_assign);
  NodeId string_literal(bool can_assign);
  NodeId call(bool can_assign);
  NodeId cast(bool can_assign);
  NodeId indexing(bool can_assign);
  NodeId member_access(bool can_assign);
};

} // namespace Parser