#include "../inc/Lexer.hpp"
#include <cstring>

namespace Lexer {

// --- StringPool ---

StringPool::StringPool(Memory::Arena &arena) : arena(arena) {
  // ID 0 зарезервирован для пустой строки
  intern("");
}

IdentId StringPool::intern(std::string_view str) {
  auto it = lookup.find(str);
  if (it != lookup.end())
    return it->second;

  // Копируем строку в Арену
  char *mem = static_cast<char *>(arena.alloc(str.size() + 1, 1));
  std::memcpy(mem, str.data(), str.size());
  mem[str.size()] = '\0';

  std::string_view interned(mem, str.size());
  IdentId id = static_cast<IdentId>(strings.size());
  strings.push_back(interned);
  lookup[interned] = id;
  return id;
}

std::string_view StringPool::get(IdentId id) const { return strings.at(id); }

// --- Scanner ---

Scanner::Scanner(std::string_view source, StringPool &pool)
    : source(source), pool(pool) {}

char Scanner::advance() {
  char c = source[current++];
  if (c == '\n') {
    line++;
    column = 1;
  } else {
    column++;
  }
  return c;
}

char Scanner::peek() const {
  if (is_at_end())
    return '\0';
  return source[current];
}

char Scanner::peek_next() const {
  if (current + 1 >= source.length())
    return '\0';
  return source[current + 1];
}

bool Scanner::match(char expected) {
  if (is_at_end() || source[current] != expected)
    return false;
  advance();
  return true;
}

void Scanner::skip_whitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
    case '\n':
      advance();
      break;
    case '/':
      if (peek_next() == '/') {
        while (peek() != '\n' && !is_at_end())
          advance();
      } else if (peek_next() == '*') {
        scan_comment();
      } else
        return;
      break;
    default:
      return;
    }
  }
}

void Scanner::scan_comment() {
  advance(); // /
  advance(); // *
  int depth = 1;
  while (depth > 0 && !is_at_end()) {
    if (peek() == '/' && peek_next() == '*') {
      advance();
      advance();
      depth++;
    } else if (peek() == '*' && peek_next() == '/') {
      advance();
      advance();
      depth--;
    } else {
      advance();
    }
  }
}

Token Scanner::make_token(TokenType type) {
  std::string_view text = source.substr(start, current - start);
  IdentId data = 0;
  if (type == TokenType::Identifier || type == TokenType::String) {
    data = pool.intern(text);
  }
  return {type, data, line, column - (int)text.length(), text};
}

Token Scanner::scan_string() {
  while (peek() != '"' && !is_at_end()) {
    if (peek() == '\\')
      advance(); // Экранирование
    advance();
  }
  if (is_at_end())
    return make_error("Незавершенная строка");
  advance(); // Закрывающая кавычка
  // Убираем кавычки из интернирования
  std::string_view value = source.substr(start + 1, current - start - 2);
  return {TokenType::String, pool.intern(value), line, column, value};
}

Token Scanner::scan_char() {
  if (is_at_end())
    return make_error("Незавершенный символьный литерал");
  
  char c = peek();
  if (c == '\'') {
    advance(); // Поглотить '\''
    return make_error("Пустой символьный литерал");
  }
  
  if (c == '\\') {
    advance(); // Поглотить '\\'
    if (is_at_end())
      return make_error("Незавершенный символьный литерал");
    advance(); // Поглотить экранируемый символ (допускать только определенный набор экранируемых символов)
  } else {
    advance();
    // Поддержка UTF-8
    while (!is_at_end() && (unsigned char)peek() >= 128 && (unsigned char)peek() < 192) { // ??
      advance();
    }
  }
  
  if (peek() != '\'') {
    return make_error("Ожидалась закрывающая кавычка '\''");
  }
  advance(); // Поглотить закрывающую кавычку
  
  // Извлекаем значение без внешних кавычек
  std::string_view value = source.substr(start + 1, current - start - 2);
  return {TokenType::Char, pool.intern(value), line, column, value};
}

Token Scanner::scan_number() {
  bool is_float = false;
  while (isdigit(peek()))
    advance();
  if (peek() == '.' && isdigit(peek_next())) {
    is_float = true;
    advance();
    while (isdigit(peek()))
      advance();
  }
  return make_token(is_float ? TokenType::Float : TokenType::Int);
}

TokenType Scanner::check_keyword(std::string_view text) {
  static const std::unordered_map<std::string_view, TokenType> keywords = {
      {"целое", TokenType::KwInt},
      {"вещ", TokenType::KwFloat},      {"лог", TokenType::KwBool},
      {"символ", TokenType::KwChar},    {"строка", TokenType::KwString},
      {"пусто", TokenType::KwVoid},     {"пост", TokenType::KwConst},
      {"если", TokenType::KwIf},        {"иначе", TokenType::KwElse},
      {"пока", TokenType::KwWhile},
      {"прервать", TokenType::KwBreak}, {"продолжить", TokenType::KwContinue},
      {"вернуть", TokenType::KwReturn}, {"истина", TokenType::KwTrue},
      {"ложь", TokenType::KwFalse},     {"структура", TokenType::KwStruct},
      {"тип", TokenType::KwType},       {"массив", TokenType::KwArray},
      {"функция", TokenType::KwFunc},   {"пространство", TokenType::KwNamespace},
      {"как", TokenType::KwAs}};
  auto it = keywords.find(text);
  return (it != keywords.end()) ? it->second : TokenType::Identifier;
}

Token Scanner::scan_identifier() {
  // Поддержка UTF-8: идентификатор может начинаться с не-ASCII или буквы
  while (isalnum(peek()) || (unsigned char)peek() > 127 || peek() == '_') {
    advance();
  }
  std::string_view text = source.substr(start, current - start);
  return make_token(check_keyword(text));
}

std::vector<Token> Scanner::tokenize() {
  std::vector<Token> tokens;
  while (!is_at_end()) {
    skip_whitespace();
    if (is_at_end())
      break;
    start = current;
    char c = advance();

    if (isdigit(c))
      tokens.push_back(scan_number());
    else if (isalpha(c) || (unsigned char)c > 127 || c == '_')
      tokens.push_back(scan_identifier());
    else
      switch (c) {
      case '(':
        tokens.push_back(make_token(TokenType::LParen));
        break;
      case ')':
        tokens.push_back(make_token(TokenType::RParen));
        break;
      case '{':
        tokens.push_back(make_token(TokenType::LBrace));
        break;
      case '}':
        tokens.push_back(make_token(TokenType::RBrace));
        break;
      case '[':
        tokens.push_back(make_token(TokenType::LBracket));
        break;
      case ']':
        tokens.push_back(make_token(TokenType::RBracket));
        break;
      case ';':
        tokens.push_back(make_token(TokenType::Semicolon));
        break;
      case ',':
        tokens.push_back(make_token(TokenType::Comma));
        break;
      case '.':
        tokens.push_back(make_token(TokenType::Dot));
        break;
      case ':':
        tokens.push_back(
            make_token(match(':') ? TokenType::ColonColon : TokenType::Colon));
        break;
      case '+':
        tokens.push_back(make_token(TokenType::Plus));
        break;
      case '-':
        tokens.push_back(make_token(TokenType::Minus));
        break;
      case '*':
        tokens.push_back(make_token(TokenType::Star));
        break;
      case '/':
        tokens.push_back(make_token(TokenType::Slash));
        break;
      case '%':
        tokens.push_back(make_token(TokenType::Percent));
        break;
      case '!':
        tokens.push_back(
            make_token(match('=') ? TokenType::BangEqual : TokenType::Bang));
        break;
      case '=':
        tokens.push_back(
            make_token(match('=') ? TokenType::EqualEqual : TokenType::Equal));
        break;
      case '<':
        tokens.push_back(
            make_token(match('=') ? TokenType::LessEqual : TokenType::Less));
        break;
      case '>':
        tokens.push_back(make_token(match('=') ? TokenType::GreaterEqual
                                               : TokenType::Greater));
        break;
      case '&':
        if (match('&'))
          tokens.push_back(make_token(TokenType::AmpAmp));
        else
          tokens.push_back(make_error("Неизвестный символ '&' (ожидалось '&&')"));
        break;
      case '|':
        if (match('|'))
          tokens.push_back(make_token(TokenType::PipePipe));
        else
          tokens.push_back(make_error("Неизвестный символ '|' (ожидалось '||')"));
        break;
      case '"':
        tokens.push_back(scan_string());
        break;
      case '\'':
        tokens.push_back(scan_char());
        break;
      default:
        tokens.push_back(make_error("Неизвестный символ"));
        break;
      }
  }
  tokens.push_back({TokenType::Eof, 0, line, column, ""});
  return tokens;
}

Token Scanner::make_error(std::string_view message) {
  return {TokenType::Error, pool.intern(message), line, column, message};
}

} // namespace Lexer