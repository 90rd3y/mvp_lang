#pragma once

#include "Memory.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Lexer {

// Типы токенов
enum class TokenType {
  // Литералы и идентификаторы
  Identifier,
  String,
  Int,
  Float,
  Char,

  // Ключевые слова
  KwInt,
  KwUint,
  KwFloat,
  KwBool,
  KwChar,
  KwString,
  KwVoid,
  KwIf,
  KwElse,
  KwWhile,
  KwBreak,
  KwContinue,
  KwReturn,
  KwTrue,
  KwFalse,
  KwStruct,
  KwType,
  KwArray,
  KwFunc,
  KwNamespace,
  KwAs,

  // Операторы и разделители
  Plus,
  Minus,
  Star,
  Slash,
  Percent, // + - * / %
  Equal,
  EqualEqual,
  BangEqual, // = == !=
  Less,
  LessEqual,
  Greater,
  GreaterEqual, // < <= > >=
  AmpAmp,
  PipePipe,
  Bang, // && || !
  Dot,
  Comma,
  Colon,
  ColonColon,
  Semicolon, // . , : :: ;
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket, // ( ) { } [ ]

  Eof,
  Error
};

// Уникальный идентификатор строки/имени
using IdentId = uint32_t;

struct Token {
  TokenType type;
  IdentId data; // Индекс для строк/идентификаторов или значение (упрощенно)
  int line;
  int column;
  std::string_view lexeme; // Для отладки
};

/**
 * @brief Таблица интернирования строк.
 * Хранит уникальные строки в Арене и выдает их ID.
 */
class StringPool {
public:
  StringPool(Memory::Arena &arena);
  IdentId intern(std::string_view str);
  std::string_view get(IdentId id) const;

private:
  Memory::Arena &arena;
  std::vector<std::string_view> strings;
  std::unordered_map<std::string_view, IdentId> lookup;
};

/**
 * @brief Сканер исходного кода.
 */
class Scanner {
public:
  Scanner(std::string_view source, StringPool &pool);
  std::vector<Token> tokenize();

private:
  std::string_view source;
  StringPool &pool;
  size_t start = 0;
  size_t current = 0;
  int line = 1;
  int column = 1;

  bool is_at_end() const { return current >= source.length(); }
  char advance();
  char peek() const;
  char peek_next() const;
  bool match(char expected);

  void skip_whitespace();
  void scan_comment();

  Token make_token(TokenType type);
  Token make_error(std::string_view message);
  Token scan_string();
  Token scan_number();
  Token scan_identifier();

  static TokenType check_keyword(std::string_view text);
};

} // namespace Lexer