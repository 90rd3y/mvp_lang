#include "../inc/Lexer.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

void assert_tokens(std::string_view source, const std::vector<Lexer::TokenType>& expected_types) {
    Memory::Arena arena;
    Lexer::StringPool pool(arena);
    Lexer::Scanner scanner(source, pool);
    auto tokens = scanner.tokenize();
    
    // Ignore the Eof token at the end when comparing length
    if (tokens.size() - 1 != expected_types.size()) {
        std::cerr << "Expected " << expected_types.size() << " tokens, but got " << (tokens.size() - 1) << " for source: '" << source << "'\n";
        for (auto t : tokens) {
            std::cerr << "Token type: " << static_cast<int>(t.type) << " lexeme: '" << t.lexeme << "'\n";
        }
        assert(false && "Token count mismatch");
    }

    for (size_t i = 0; i < expected_types.size(); ++i) {
        if (tokens[i].type != expected_types[i]) {
            std::cerr << "Token " << i << " type mismatch for source: '" << source << "'. Expected " 
                      << static_cast<int>(expected_types[i]) << ", got " << static_cast<int>(tokens[i].type) 
                      << " (lexeme: '" << tokens[i].lexeme << "')\n";
            assert(false && "Token type mismatch");
        }
    }
}

void test_data_types() {
    assert_tokens("123 3.14 \"hello\" 'A' 0", {
        Lexer::TokenType::Int,
        Lexer::TokenType::Float,
        Lexer::TokenType::String,
        Lexer::TokenType::Char,
        Lexer::TokenType::Int
    });
    std::cout << "[OK] Типы данных (числа, строки, символы)\n";
}

void test_operators() {
    assert_tokens("+ - * / % = == != < <= > >= && || !", {
        Lexer::TokenType::Plus,
        Lexer::TokenType::Minus,
        Lexer::TokenType::Star,
        Lexer::TokenType::Slash,
        Lexer::TokenType::Percent,
        Lexer::TokenType::Equal,
        Lexer::TokenType::EqualEqual,
        Lexer::TokenType::BangEqual,
        Lexer::TokenType::Less,
        Lexer::TokenType::LessEqual,
        Lexer::TokenType::Greater,
        Lexer::TokenType::GreaterEqual,
        Lexer::TokenType::AmpAmp,
        Lexer::TokenType::PipePipe,
        Lexer::TokenType::Bang
    });
    std::cout << "[OK] Операторы\n";
}

void test_keywords() {
    assert_tokens("целое вещ если иначе пока прервать продолжить вернуть истина ложь структура тип массив функция пространство как пост пусто", {
        Lexer::TokenType::KwInt,
        Lexer::TokenType::KwFloat,
        Lexer::TokenType::KwIf,
        Lexer::TokenType::KwElse,
        Lexer::TokenType::KwWhile,
        Lexer::TokenType::KwBreak,
        Lexer::TokenType::KwContinue,
        Lexer::TokenType::KwReturn,
        Lexer::TokenType::KwTrue,
        Lexer::TokenType::KwFalse,
        Lexer::TokenType::KwStruct,
        Lexer::TokenType::KwType,
        Lexer::TokenType::KwArray,
        Lexer::TokenType::KwFunc,
        Lexer::TokenType::KwNamespace,
        Lexer::TokenType::KwAs,
        Lexer::TokenType::KwConst,
        Lexer::TokenType::KwVoid
    });
    std::cout << "[OK] Ключевые слова\n";
}

void test_whitespace_and_comments() {
    assert_tokens("  \t \n \r 42", { Lexer::TokenType::Int });
    
    assert_tokens("123 // это однострочный комментарий\n 456", { 
        Lexer::TokenType::Int, 
        Lexer::TokenType::Int 
    });

    assert_tokens("123 /* это \n многострочный \n комментарий */ 456", { 
        Lexer::TokenType::Int, 
        Lexer::TokenType::Int 
    });

    assert_tokens("/* вложенный /* комментарий */ */ 789", {
        Lexer::TokenType::Int
    });

    std::cout << "[OK] Пробелы и комментарии\n";
}

void test_identifiers() {
    assert_tokens("myVar _hiddenVar var123 имя_переменной", {
        Lexer::TokenType::Identifier,
        Lexer::TokenType::Identifier,
        Lexer::TokenType::Identifier,
        Lexer::TokenType::Identifier
    });
    std::cout << "[OK] Идентификаторы\n";
}

void test_delimiters() {
    assert_tokens(". , : :: ; ( ) { } [ ]", {
        Lexer::TokenType::Dot,
        Lexer::TokenType::Comma,
        Lexer::TokenType::Colon,
        Lexer::TokenType::ColonColon,
        Lexer::TokenType::Semicolon,
        Lexer::TokenType::LParen,
        Lexer::TokenType::RParen,
        Lexer::TokenType::LBrace,
        Lexer::TokenType::RBrace,
        Lexer::TokenType::LBracket,
        Lexer::TokenType::RBracket
    });
    std::cout << "[OK] Разделители\n";
}

void test_errors() {
    assert_tokens("\"hello", { Lexer::TokenType::Error });
    assert_tokens("@", { Lexer::TokenType::Error });
    assert_tokens("&", { Lexer::TokenType::Error });
    std::cout << "[OK] Обработка ошибок (неверные символы, незавершенные строки)\n";
}

int main() {
    std::cout << "=== Запуск тестов Lexer ===\n";
    
    test_data_types();
    test_operators();
    test_keywords();
    test_whitespace_and_comments();
    test_identifiers();
    test_delimiters();
    test_errors();
    
    std::cout << "=== Все тесты лексера успешно пройдены! ===\n";
    return 0;
}
