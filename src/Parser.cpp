#include "../inc/Parser.hpp"
#include <iostream>

namespace Parser {

Parser::Parser(const std::vector<Lexer::Token> &t, Lexer::StringPool &p) : tokens(t), pool(p) {
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
            << "] статическая ошибка: " << message << std::endl;
  // В учебном проекте просто выходим
  std::exit(1);
}

Lexer::Token Parser::parse_type_token() {
    if (check(Lexer::TokenType::KwInt) || check(Lexer::TokenType::KwFloat) ||
        check(Lexer::TokenType::KwBool) || check(Lexer::TokenType::KwChar) ||
        check(Lexer::TokenType::KwString) || check(Lexer::TokenType::KwVoid)) {
        return advance();
    }
    if (check(Lexer::TokenType::Identifier)) {
        Lexer::Token name = advance();
        std::string full_name = std::string(name.lexeme);
        while (match(Lexer::TokenType::ColonColon)) {
            Lexer::Token part = consume(Lexer::TokenType::Identifier, "Ожидалось имя после '::'");
            full_name += "::" + std::string(part.lexeme);
        }
        Lexer::Token full_token = name;
        full_token.data = pool.intern(full_name);
        full_token.lexeme = pool.get(full_token.data);
        return full_token;
    }
    error(peek(), "Ожидалось имя типа");
    return advance();
}

bool Parser::is_type_token(size_t index) const {
    if (index >= tokens.size()) return false;
    Lexer::TokenType t = tokens[index].type;
    if (t == Lexer::TokenType::KwInt || t == Lexer::TokenType::KwFloat ||
        t == Lexer::TokenType::KwBool || t == Lexer::TokenType::KwChar ||
        t == Lexer::TokenType::KwString) {
        return true;
    }
    
    if (t == Lexer::TokenType::Identifier) {
        size_t next_idx = index + 1;
        
        // Поддержка пространств имен (std::Точка)
        while (next_idx < tokens.size() && tokens[next_idx].type == Lexer::TokenType::ColonColon) {
            next_idx++;
            if (next_idx < tokens.size() && tokens[next_idx].type == Lexer::TokenType::Identifier) {
                next_idx++;
            } else {
                return false;
            }
        }

        if (next_idx < tokens.size() && tokens[next_idx].type == Lexer::TokenType::LBracket) {
            next_idx++;
            if (next_idx < tokens.size() && tokens[next_idx].type == Lexer::TokenType::Int) {
                next_idx++;
            }
            if (next_idx < tokens.size() && tokens[next_idx].type == Lexer::TokenType::RBracket) {
                next_idx++;
            } else {
                return false;
            }
        }
        if (next_idx < tokens.size() && tokens[next_idx].type == Lexer::TokenType::Identifier) {
            return true;
        }
    }
    
    return false;
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
  case Lexer::TokenType::Char:
    return create_node(NodeType::LiteralChar, previous());
  case Lexer::TokenType::KwTrue:
  case Lexer::TokenType::KwFalse:
    return create_node(NodeType::LiteralBool, previous());
  default:
    return InvalidNode;
  }
}

NodeId Parser::variable(bool) {
    Lexer::Token name = previous();
    std::string full_name = std::string(name.lexeme);
    while (match(Lexer::TokenType::ColonColon)) {
        Lexer::Token part = consume(Lexer::TokenType::Identifier, "Ожидалось имя после '::'");
        full_name += "::" + std::string(part.lexeme);
    }
    Lexer::Token full_token = name;
    full_token.data = pool.intern(full_name);
    full_token.lexeme = pool.get(full_token.data);

    // Если после идентификатора идет '{', это литерал структуры
    if (match(Lexer::TokenType::LBrace)) {
        std::vector<NodeId> elements;
        elements.push_back(create_node(NodeType::Identifier, full_token)); // Имя структуры
        if (!check(Lexer::TokenType::RBrace)) {
            do {
                elements.push_back(expression());
            } while (match(Lexer::TokenType::Comma));
        }
        consume(Lexer::TokenType::RBrace, "Ожидалось '}'");
        return create_node(NodeType::StructLiteral, full_token, elements);
    }

    return create_node(NodeType::Identifier, full_token);
}

NodeId Parser::array_literal(bool) {
    Lexer::Token bracket = previous();
    std::vector<NodeId> elements;
    if (!check(Lexer::TokenType::RBracket)) {
        do {
            if (check(Lexer::TokenType::LBracket)) {
                error(peek(), "Вложенные литералы массивов запрещены (массивы могут быть только одномерными)");
            }
            elements.push_back(expression());
        } while (match(Lexer::TokenType::Comma));
    }
    consume(Lexer::TokenType::RBracket, "Ожидалось ']'");
    return create_node(NodeType::ArrayLiteral, bracket, elements);
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
    r[static_cast<int>(Lexer::TokenType::LParen)] = {&Parser::grouping, &Parser::call, PREC_CALL};
    r[static_cast<int>(Lexer::TokenType::Plus)] = {nullptr, &Parser::binary, PREC_TERM};
    r[static_cast<int>(Lexer::TokenType::Minus)] = {&Parser::unary, &Parser::binary, PREC_TERM};
    r[static_cast<int>(Lexer::TokenType::Star)] = {nullptr, &Parser::binary, PREC_FACTOR};
    r[static_cast<int>(Lexer::TokenType::Slash)] = {nullptr, &Parser::binary, PREC_FACTOR};
    r[static_cast<int>(Lexer::TokenType::Percent)] = {nullptr, &Parser::binary, PREC_FACTOR};
    r[static_cast<int>(Lexer::TokenType::Less)] = {nullptr, &Parser::binary, PREC_COMPARISON};
    r[static_cast<int>(Lexer::TokenType::LessEqual)] = {nullptr, &Parser::binary, PREC_COMPARISON};
    r[static_cast<int>(Lexer::TokenType::Greater)] = {nullptr, &Parser::binary, PREC_COMPARISON};
    r[static_cast<int>(Lexer::TokenType::GreaterEqual)] = {nullptr, &Parser::binary, PREC_COMPARISON};
    r[static_cast<int>(Lexer::TokenType::EqualEqual)] = {nullptr, &Parser::binary, PREC_EQUALITY};
    r[static_cast<int>(Lexer::TokenType::BangEqual)] = {nullptr, &Parser::binary, PREC_EQUALITY};
    r[static_cast<int>(Lexer::TokenType::AmpAmp)] = {nullptr, &Parser::binary, PREC_AND};
    r[static_cast<int>(Lexer::TokenType::PipePipe)] = {nullptr, &Parser::binary, PREC_OR};
    r[static_cast<int>(Lexer::TokenType::Bang)] = {&Parser::unary, nullptr, PREC_UNARY};
    r[static_cast<int>(Lexer::TokenType::Int)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::Float)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::String)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::Char)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::KwTrue)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::KwFalse)] = {&Parser::literal, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::Identifier)] = {&Parser::variable, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::KwAs)] = {&Parser::cast, nullptr, PREC_NONE};
    r[static_cast<int>(Lexer::TokenType::LBracket)] = {&Parser::array_literal, &Parser::indexing, PREC_CALL};
    r[static_cast<int>(Lexer::TokenType::Dot)] = {nullptr, &Parser::member_access, PREC_CALL};
    return r;
  }();
  return &rules[static_cast<int>(type)];
}

// --- Рекурсивный спуск ---

NodeId Parser::declaration() {
  if (match(Lexer::TokenType::KwNamespace))
    return namespace_declaration();
  if (match(Lexer::TokenType::KwFunc))
    return func_declaration();
  if (match(Lexer::TokenType::KwStruct))
    return struct_declaration();
  if (match(Lexer::TokenType::KwType))
    return type_alias();
    
  bool is_mutable = !match(Lexer::TokenType::KwConst);

  if (is_type_token(current)) {
    return var_declaration(is_mutable);
  }
  
  if (!is_mutable) {
      error(previous(), "Ожидалось указание типа после ключевого слова 'пост'");
  }
  
  return statement();
}

NodeId Parser::var_declaration(bool is_mutable) {
    Lexer::Token type_token = parse_type_token();
    uint32_t array_size = 0;
    if (match(Lexer::TokenType::LBracket)) {
        Lexer::Token size_tok = consume(Lexer::TokenType::Int, "Ожидался размер массива");
        array_size = std::stoul(std::string(size_tok.lexeme));
        consume(Lexer::TokenType::RBracket, "Ожидалось ']'");
        if (check(Lexer::TokenType::LBracket)) {
            error(peek(), "Многомерные массивы запрещены (массивы могут быть только одномерными)");
        }
    }
    Lexer::Token name = consume(Lexer::TokenType::Identifier, "Ожидалось имя переменной");
    
    // СТРОГАЯ ИНИЦИАЛИЗАЦИЯ ДЛЯ ВСЕХ ТИПОВ
    consume(Lexer::TokenType::Equal, "Ожидалась обязательная инициализация переменной '='");
    NodeId initializer = expression();
    consume(Lexer::TokenType::Semicolon, "Ожидалось ';' после объявления");
    
    NodeId type_node = create_node(NodeType::Identifier, type_token);
    NodeId node = create_node(NodeType::VarDecl, name, {initializer, type_node});
    nodes[node].extra_data = array_size;
    nodes[node].is_mutable = is_mutable;
    return node;
}

NodeId Parser::block() {
    Lexer::Token brace = previous();
    std::vector<NodeId> stmts;
    while (!check(Lexer::TokenType::RBrace) && !check(Lexer::TokenType::Eof)) {
        bool is_mutable = !match(Lexer::TokenType::KwConst);

        if (is_type_token(current)) {
            stmts.push_back(var_declaration(is_mutable));
        } else if (!is_mutable) {
            error(previous(), "Ожидалось указание типа после ключевого слова 'пост'");
        } else {
            stmts.push_back(statement());
        }
    }
    consume(Lexer::TokenType::RBrace, "Ожидалось '}'");
    return create_node(NodeType::Block, brace, stmts);
}

NodeId Parser::statement() {
  if (match(Lexer::TokenType::KwIf))
    return if_statement();
  if (match(Lexer::TokenType::KwWhile))
    return while_statement();
  if (match(Lexer::TokenType::KwReturn))
    return return_statement();
  if (match(Lexer::TokenType::KwBreak)) {
      consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
      return create_node(NodeType::Break, previous());
  }
  if (match(Lexer::TokenType::KwContinue)) {
      consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
      return create_node(NodeType::Continue, previous());
  }
  if (match(Lexer::TokenType::LBrace))
    return block();

  NodeId expr = expression();
  if (match(Lexer::TokenType::Equal)) {
      if (nodes[expr].type != NodeType::Identifier && 
          nodes[expr].type != NodeType::Indexing && 
          nodes[expr].type != NodeType::MemberAccess) {
          error(previous(), "Недопустимое выражение слева от оператора присваивания '='");
      }
      Lexer::Token eq_tok = previous();
      NodeId value = expression();
      consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
      return create_node(NodeType::AssignStmt, eq_tok, {expr, value});
  }
  consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
  if (nodes[expr].type != NodeType::Call) {
      error(previous(), "Инструкция-выражение может быть только вызовом функции");
  }
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
NodeId Parser::call(bool) {
    NodeId callee = static_cast<NodeId>(nodes.size() - 1); // Левый узел (имя функции)
    std::vector<NodeId> args;
    args.push_back(callee);
    if (!check(Lexer::TokenType::RParen)) {
        do {
            args.push_back(expression());
        } while (match(Lexer::TokenType::Comma));
    }
    consume(Lexer::TokenType::RParen, "Ожидалось ')' после аргументов");
    return create_node(NodeType::Call, previous(), args);
}

NodeId Parser::func_declaration() {
    Lexer::Token name = consume(Lexer::TokenType::Identifier, "Ожидалось имя функции");
    consume(Lexer::TokenType::LParen, "Ожидалось '('");
    std::vector<NodeId> children;
    
    children.push_back(InvalidNode); // Заглушка для типа возврата (индекс 0)
    children.push_back(InvalidNode); // Заглушка для тела (индекс 1)
    
    if (!check(Lexer::TokenType::RParen)) {
        do {
            Lexer::Token type_tok = parse_type_token(); // Тип аргумента
            uint32_t array_size = 0;
            if (match(Lexer::TokenType::LBracket)) {
                if (check(Lexer::TokenType::Int)) {
                    Lexer::Token size_tok = advance();
                    array_size = std::stoul(std::string(size_tok.lexeme));
                }
                consume(Lexer::TokenType::RBracket, "Ожидалось ']'");
            }
            Lexer::Token arg_name = consume(Lexer::TokenType::Identifier, "Ожидалось имя аргумента");
            NodeId type_node = create_node(NodeType::Identifier, type_tok);
            nodes[type_node].extra_data = array_size;
            children.push_back(type_node);
            children.push_back(create_node(NodeType::Identifier, arg_name));
        } while (match(Lexer::TokenType::Comma));
    }
    consume(Lexer::TokenType::RParen, "Ожидалось ')'");
    
    consume(Lexer::TokenType::Colon, "Ожидалось ':'");
    Lexer::Token ret_type = parse_type_token();
    uint32_t ret_array_size = 0;
    if (match(Lexer::TokenType::LBracket)) {
        if (check(Lexer::TokenType::Int)) {
            Lexer::Token size_tok = advance();
            ret_array_size = std::stoul(std::string(size_tok.lexeme));
        }
        consume(Lexer::TokenType::RBracket, "Ожидалось ']'");
    }
    NodeId ret_type_node = create_node(NodeType::Identifier, ret_type);
    nodes[ret_type_node].extra_data = ret_array_size;
    children[0] = ret_type_node;
    
    if (match(Lexer::TokenType::Semicolon)) {
        children[1] = InvalidNode;
    } else {
        consume(Lexer::TokenType::LBrace, "Ожидалось '{' перед телом функции");
        children[1] = block(); // Тело функции
    }
    
    return create_node(NodeType::FuncDecl, name, children);
}

NodeId Parser::namespace_declaration() {
    Lexer::Token name = consume(Lexer::TokenType::Identifier, "Ожидалось имя пространства имен");
    consume(Lexer::TokenType::LBrace, "Ожидалось '{'");
    std::vector<NodeId> decls;
    while (!check(Lexer::TokenType::RBrace) && !check(Lexer::TokenType::Eof)) {
        decls.push_back(declaration());
    }
    consume(Lexer::TokenType::RBrace, "Ожидалось '}'");
    return create_node(NodeType::NamespaceDecl, name, decls);
}
NodeId Parser::struct_declaration() {
    Lexer::Token name = consume(Lexer::TokenType::Identifier, "Ожидалось имя структуры");
    consume(Lexer::TokenType::LBrace, "Ожидалось '{'");
    std::vector<NodeId> fields;
    while (!check(Lexer::TokenType::RBrace) && !check(Lexer::TokenType::Eof)) {
        Lexer::Token type_tok = parse_type_token();
        uint32_t array_size = 0;
        if (match(Lexer::TokenType::LBracket)) {
            if (check(Lexer::TokenType::Int)) {
                Lexer::Token size_tok = advance();
                array_size = std::stoul(std::string(size_tok.lexeme));
            }
            consume(Lexer::TokenType::RBracket, "Ожидалось ']'");
        }
        Lexer::Token field_name = consume(Lexer::TokenType::Identifier, "Ожидалось имя поля");
        consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
        NodeId type_node = create_node(NodeType::Identifier, type_tok);
        nodes[type_node].extra_data = array_size;
        fields.push_back(type_node);
        fields.push_back(create_node(NodeType::Identifier, field_name));
    }
    
    if (fields.empty()) {
        error(previous(), "Структура должна содержать хотя бы одно поле");
    }
    
    consume(Lexer::TokenType::RBrace, "Ожидалось '}'");
    return create_node(NodeType::StructDecl, name, fields);
}
NodeId Parser::type_alias() {
    Lexer::Token name = consume(Lexer::TokenType::Identifier, "Ожидалось имя синонима");
    consume(Lexer::TokenType::Equal, "Ожидалось '='");
    Lexer::Token base_type = parse_type_token();
    uint32_t array_size = 0;
    if (match(Lexer::TokenType::LBracket)) {
        if (check(Lexer::TokenType::Int)) {
            Lexer::Token size_tok = advance();
            array_size = std::stoul(std::string(size_tok.lexeme));
        }
        consume(Lexer::TokenType::RBracket, "Ожидалось ']'");
    }
    consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
    NodeId type_node = create_node(NodeType::Identifier, base_type);
    nodes[type_node].extra_data = array_size;
    return create_node(NodeType::TypeAlias, name, {type_node});
}
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
NodeId Parser::return_statement() {
    Lexer::Token ret_tok = previous();
    NodeId expr = InvalidNode;
    if (!check(Lexer::TokenType::Semicolon)) {
        expr = expression();
    }
    consume(Lexer::TokenType::Semicolon, "Ожидалось ';'");
    return create_node(NodeType::Return, ret_tok, expr != InvalidNode ? std::vector<NodeId>{expr} : std::vector<NodeId>{});
}

NodeId Parser::cast(bool) {
    Lexer::Token as_tok = previous();
    consume(Lexer::TokenType::Less, "Ожидалось '<' после 'как'");
    Lexer::Token type_tok = parse_type_token();
    uint32_t array_size = 0;
    if (match(Lexer::TokenType::LBracket)) {
        if (check(Lexer::TokenType::Int)) {
            Lexer::Token size_tok = advance();
            array_size = std::stoul(std::string(size_tok.lexeme));
        }
        consume(Lexer::TokenType::RBracket, "Ожидалось ']'");
    }
    consume(Lexer::TokenType::Greater, "Ожидалось '>' после типа");
    consume(Lexer::TokenType::LParen, "Ожидалось '(' после '<Type>'");
    NodeId expr = expression();
    consume(Lexer::TokenType::RParen, "Ожидалось ')'");
    NodeId type_node = create_node(NodeType::Identifier, type_tok);
    nodes[type_node].extra_data = array_size;
    return create_node(NodeType::Cast, as_tok, {type_node, expr});
}

NodeId Parser::indexing(bool) {
    NodeId left = static_cast<NodeId>(nodes.size() - 1);
    NodeId index = expression();
    consume(Lexer::TokenType::RBracket, "Ожидалось ']'");

    return create_node(NodeType::Indexing, previous(), {left, index});
}

NodeId Parser::member_access(bool) {
    NodeId left = static_cast<NodeId>(nodes.size() - 1);
    Lexer::Token field_name = consume(Lexer::TokenType::Identifier, "Ожидалось имя поля после '.'");
    return create_node(NodeType::MemberAccess, field_name, {left});
}

} // namespace Parser