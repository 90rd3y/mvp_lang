#include <iostream>
#include <string_view>
#include <unordered_map>
enum class TokenType { KwConst, Identifier };
TokenType check_keyword(std::string_view text) {
  static const std::unordered_map<std::string_view, TokenType> keywords = {
      {"пост", TokenType::KwConst}
  };
  auto it = keywords.find(text);
  return (it != keywords.end()) ? it->second : TokenType::Identifier;
}
int main() {
  std::cout << (check_keyword("пост") == TokenType::KwConst ? "KwConst" : "Identifier") << std::endl;
}
