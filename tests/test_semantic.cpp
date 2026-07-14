#include "../inc/Semantic.hpp"
#include "../inc/Parser.hpp"
#include "../inc/Lexer.hpp"
#include "../inc/Memory.hpp"
#include <iostream>
#include <cassert>
#include <unistd.h>
#include <sys/wait.h>

void run_semantic(std::string_view source) {
    Memory::Arena ast_arena;
    Lexer::StringPool pool(ast_arena);
    Lexer::Scanner scanner(source, pool);
    auto tokens = scanner.tokenize();
    
    Parser::Parser parser(tokens, pool);
    auto root = parser.parse();
    
    Semantic::Analyzer semantic(parser.get_nodes(), parser.get_child_indices(), pool);
    semantic.analyze(root);
}

void test_success(std::string_view source, const char* test_name) {
    run_semantic(source);
    std::cout << "[OK] " << test_name << "\n";
}

void test_fail(std::string_view source, const char* test_name) {
    std::cout << "[INFO] Ожидается ошибка для: " << test_name << "...\n";
    std::cout << "       ---> ";
    std::flush(std::cout);

    pid_t pid = fork();
    if (pid == 0) {
        run_semantic(source);
        std::exit(0); // Если выполнение дошло сюда, значит анализатор не выбросил ошибку
    } else {
        int status;
        waitpid(pid, &status, 0);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 1 && "Анализатор НЕ завершился с кодом 1 при семантической ошибке!");
        std::cout << "[OK] Корректно поймана ошибка: " << test_name << "\n";
    }
}

void test_scopes() {
    test_success(
        "функция Начало() : целое {\n"
        "  целое x = 1;\n"
        "  {\n"
        "    целое x = 2;\n"
        "  }\n"
        "  вернуть x;\n"
        "}", "Области видимости (затенение локальной переменной)");

    test_fail(
        "функция Начало() : целое {\n"
        "  {\n"
        "    целое x = 2;\n"
        "  }\n"
        "  вернуть x;\n"
        "}", "Переменная вне области видимости");
        
    test_fail(
        "функция Начало() : целое {\n"
        "  целое x = 1;\n"
        "  целое x = 2;\n"
        "  вернуть x;\n"
        "}", "Повторное объявление переменной в одном блоке");
}

void test_type_checking() {
    test_success(
        "функция Начало() : целое {\n"
        "  целое a = 5;\n"
        "  вещ b = 3.14;\n"
        "  лог c = истина;\n"
        "  вернуть a;\n"
        "}", "Корректные типы базовых переменных");

    test_fail(
        "функция Начало() : целое {\n"
        "  целое a = 3.14;\n"
        "  вернуть a;\n"
        "}", "Несоответствие типов при инициализации (вещ -> целое)");

    test_fail(
        "функция Начало() : целое {\n"
        "  целое a = 5;\n"
        "  лог b = истина;\n"
        "  целое c = a + b;\n"
        "  вернуть c;\n"
        "}", "Недопустимые типы в бинарной операции (+ для лог)");
}

void test_execution_contexts() {
    test_success(
        "функция тест() : пусто {\n"
        "  вернуть;\n"
        "}\n"
        "функция Начало() : целое {\n"
        "  тест();\n"
        "  вернуть 0;\n"
        "}", "Корректный возврат из функции (пусто)");

    test_fail(
        "функция Начало() : пусто {\n"
        "  вернуть 0;\n"
        "}", "Несоответствие типа возвращаемого значения (целое вместо пусто)");

    test_fail(
        "пост целое a = 10;\n"
        "функция Начало() : целое {\n"
        "  a = 20;\n"
        "  вернуть a;\n"
        "}", "Изменение константы");
}

int main() {
    std::cout << "=== Запуск тестов Semantic Analyzer ===\n";
    
    test_scopes();
    test_type_checking();
    test_execution_contexts();
    
    std::cout << "=== Все тесты семантики успешно пройдены! ===\n";
    return 0;
}
