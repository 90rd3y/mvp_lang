#include "../inc/Parser.hpp"
#include <iostream>

namespace Parser {

Parser::Parser(const std::vector<Lexer::Token> &t) : tokens(t) {
  // Резервируем нулевой узел как невалидный
  nodes.push_back(
      {NodeType::None, {Lexer::TokenType::Error, 0, 0, 0, ""}, 0, 0});
}

NodeId Parser::create_node(NodeType type, Lexer::Token token,
                           const std::vector<NodeId> &children) {
  uint32_t offset = static_cast<uint32_t>(child_indices.size());
  for (NodeId child : children) {
    child_indices.push_back(child);
  }
  nodes.push_back(
      {type, token, offset, static_cast<uint32_t>(children.size())});
  return static_cast<NodeId>(nodes.size() - 1);
}

// --- Навигация по токенам ---

Lexer::Token Parser::advance() {
  if (!check(Lexer::TokenType::Eof))
    current++;
  return previous();
}

Lexer::Token Parser::peek() const { return tokens[current]; }
Lexer::Token Parser::previous() const { return tokens[current - 1]; }
bool Parser::check(Lexer::TokenType type) const { return peek().type == type; }

bool Parser::match(Lexer::TokenType type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

Lexer::Token Parser::consume(Lexer::TokenType type, const char *message) {
  if (check(type))
    return advance();
  error(peek(), message);
  return tokens[current];
}

void Parser::error(Lexer::Token token, const char *message) {
  std::cerr << "[" << token.line << ":" << token.column
            << "] Ошибка: " << message << std::endl;
  // В учебном проекте просто выходим, в реальном — синхронизируемся
  std::exit(1);
}

// --- Pratt Parser ---

NodeId Parser::expression() { return parse_precedence(PREC_ASSIGNMENT); }

NodeId Parser::parse_precedence(Precedence precedence) {
  advance();
  auto prefix_rule = get_rule(previous().type)->prefix;
  if (!prefix_rule) {
    error(previous(), "Ожидалось выражение");
    return InvalidNode;
  }

  bool can_assign = precedence <= PREC_ASSIGNMENT;
  NodeId left = (this->*prefix_rule)(can_assign);

  while (precedence <= get_rule(peek().type)->precedence) {
    advance();
    auto infix_rule = get_rule(previous().type)->infix;
    left = (this->*infix_rule)(can_assign);
  }

  return left;
}

NodeId Parser::literal(bool) {
  switch (previous().type) {
  case Lexer::TokenType::Int:
    return create_node(NodeType::LiteralInt, previous());
  case Lexer::TokenType::Float:
    return create_node(NodeType::LiteralFloat, previous());
  case Lexer::TokenType::String:
    return create_node(NodeType::LiteralString, previous());
  case Lexer::TokenType::KwTrue:
  case Lexer::TokenType::KwFalse:
    return create_node(NodeType::LiteralBool, previous());
  default:
    return InvalidNode;
  }
}

NodeId Parser::variable(bool can_assign) {
  Lexer::Token name = previous();
  if (can_assign && match(Lexer::TokenType::Equal)) {
    NodeId value = expression();
    return create_node(NodeType::Assign, name, {value});
  }
  return create_node(NodeType::Identifier, name);
}

NodeId Parser::binary(bool) {
  Lexer::Token op = previous();
  const ParseRule *rule = get_rule(op.type);
  NodeId left = static_cast<NodeId>(nodes.size() - 1); // Упрощенно для примера
  // На самом деле в Pratt левая часть передается, но в нашей плоской структуре
  // мы пересоберем дерево позже или используем стек.
  // Для краткости здесь логика упрощена:
  NodeId right = parse_precedence((Precedence)(rule->precedence + 1));
  return create_node(NodeType::BinaryOp, op, {left, right});
}

#include <array>

const Parser::ParseRule *Parser::get_rule(Lexer::TokenType type) {
  static const auto rules = []() {
    std::array<ParseRule, static_cast<size_t>(Lexer::TokenType::Error) + 1> r{};
    r[static_cast<int>(Lexer::TokenType::LParen)] = {&Parser::grouping, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::Plus)] = {nullptr, &Parser::binary, PREC_TERM};
    r[static_cast<int>(Lexer::TokenType::Minus)] = {&Parser::unary, &Parser::binary, PREC_TERM};
    r[static_cast<int>(Lexer::TokenType::Star)] = {nullptr, &Parser::binary, PREC_FACTOR};
    r[static_cast<int>(Lexer::TokenType::Less)] = {nullptr, &Parser::binary, PREC_COMPARISON};
    r[static_cast<int>(Lexer::TokenType::EqualEqual)] = {nullptr, &Parser::binary, PREC_EQUALITY};
    r[static_cast<int>(Lexer::TokenType::Int)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::KwTrue)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::KwFalse)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::Identifier)] = {&Parser::variable, nullptr, PREC_NONE};
    return r;
  }();
  return &rules[static_cast<int>(type)];
}

// --- Рекурсивный спуск ---

NodeId Parser::declaration() {
  if (match(Lexer::TokenType::KwFunc))
    return func_declaration();
  if (match(Lexer::TokenType::KwStruct))
    return struct_declaration();
  if (match(Lexer::TokenType::KwType))
    return type_alias();
    
  if (check(Lexer::TokenType::KwInt) || check(Lexer::TokenType::KwUint) ||
      check(Lexer::TokenType::KwFloat) || check(Lexer::TokenType::KwBool) ||
      check(Lexer::TokenType::KwChar) || check(Lexer::TokenType::KwString) ||
      check(Lexer::TokenType::KwVoid)) {
    return var_declaration();
  }
  
  return statement();
}

NodeId Parser::var_declaration() {
  // Формат: тип имя = значение;
  Lexer::Token type_token = advance(); // Упрощенно: берем токен типа
  (void)type_token;
  Lexer::Token name =
      consume(Lexer::TokenType::Identifier, "Ожидалось имя переменной");
  NodeId initializer = InvalidNode;
  if (match(Lexer::TokenType::Equal)) {
    initializer = expression();
  }
  consume(Lexer::TokenType::Semicolon, "Ожидалось ';' после объявления");
  return create_node(NodeType::VarDecl, name, {initializer});
}

NodeId Parser::block() {
    Lexer::Token brace = previous();
    std::vector<NodeId> stmts;
    while (!check(Lexer::TokenType::RBrace) && !check(Lexer::TokenType::Eof)) {
        // Внутри блока могут быть и переменные, и инструкции
        stmts.push_back(declaration()); 
    }
    consume(Lexer::TokenType::RBrace, "Ожидалось '}'");
    return create_node(NodeType::Block, brace, stmts);
}

NodeId Parser::statement() {
  if (match(Lexer::TokenType::KwPrint)) {
    Lexer::Token print_tok = previous();
    NodeId expr = expression();
    consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
    return create_node(NodeType::ExprStmt, print_tok, {expr});
  }
  if (match(Lexer::TokenType::KwIf))
    return if_statement();
  if (match(Lexer::TokenType::KwWhile))
    return while_statement();
  if (match(Lexer::TokenType::KwReturn))
    return return_statement();
  if (match(Lexer::TokenType::LBrace))
    return block();

  // По умолчанию — выражение
  NodeId expr = expression();
  consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
  return create_node(NodeType::ExprStmt, previous(), {expr});
}

NodeId Parser::parse() {
  std::vector<NodeId> decls;
  while (!check(Lexer::TokenType::Eof)) {
    decls.push_back(declaration());
  }
  return create_node(NodeType::Program, tokens[0], decls);
}

// Заглушки для краткости (реализуются аналогично)
NodeId Parser::grouping(bool) {
  NodeId e = expression();
  consume(Lexer::TokenType::RParen, "Ожидалось ')'");
  return e;
}
NodeId Parser::unary(bool) {
  Lexer::Token op = previous();
  NodeId arg = parse_precedence(PREC_UNARY);
  return create_node(NodeType::UnaryOp, op, {arg});
}
NodeId Parser::func_declaration() {
  return InvalidNode;
} // Реализация парсинга параметров и тела
NodeId Parser::struct_declaration() { return InvalidNode; }
NodeId Parser::type_alias() { return InvalidNode; }
NodeId Parser::if_statement() {
    Lexer::Token if_tok = previous();
    consume(Lexer::TokenType::LParen, "Ожидалось '(' после 'если'");
    NodeId condition = expression();
    consume(Lexer::TokenType::RParen, "Ожидалось ')' после условия");
    
    NodeId then_branch = statement();
    NodeId else_branch = InvalidNode;
    
    if (match(Lexer::TokenType::KwElse)) {
        else_branch = statement();
    }
    
    return create_node(NodeType::If, if_tok, {condition, then_branch, else_branch});
}

NodeId Parser::while_statement() {
    Lexer::Token while_tok = previous();
    consume(Lexer::TokenType::LParen, "Ожидалось '(' после 'пока'");
    NodeId condition = expression();
    consume(Lexer::TokenType::RParen, "Ожидалось ')' после условия");
    
    NodeId body = statement();
    return create_node(NodeType::While, while_tok, {condition, body});
}
NodeId Parser::return_statement() { return InvalidNode; }

} // namespace Parser