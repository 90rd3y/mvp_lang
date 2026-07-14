#include "../inc/Interpreter.hpp"
#include "../inc/Lexer.hpp"
#include "../inc/Parser.hpp"
#include "../inc/Semantic.hpp"
#include "../inc/Memory.hpp"
#include <iostream>
#include <cassert>
#include <string_view>
#include <unistd.h>
#include <sys/wait.h>

int run_code(std::string_view source) {
    Memory::Arena ast_arena;
    Lexer::StringPool pool(ast_arena);
    Lexer::Scanner scanner(source, pool);
    auto tokens = scanner.tokenize();
    
    Parser::Parser parser(tokens, pool);
    auto root = parser.parse();
    
    Semantic::Analyzer semantic(parser.get_nodes(), parser.get_child_indices(), pool);
    semantic.analyze(root);
    
    Memory::Arena runtime_arena;
    Interpreter::VM vm(parser.get_nodes(), parser.get_child_indices(), pool, runtime_arena);
    return vm.run(root);
}

void test_ast_execution_and_returns() {
    // Простой возврат значения
    assert(run_code("функция Начало() : целое { вернуть 42; }") == 42);
    std::cout << "[OK] Исполнение AST и возврат значений\n";
}

void test_mathematics() {
    // Приоритет операций и скобки
    assert(run_code("функция Начало() : целое { вернуть (10 + 2) * 3 / 2 - 1; }") == 17);
    
    std::cout << "[OK] Математика\n";
}

void test_branching() {
    // Условие истинно
    assert(run_code(
        "функция Начало() : целое {\n"
        "  целое a = 5;\n"
        "  если (a > 3) {\n"
        "    вернуть 1;\n"
        "  } иначе {\n"
        "    вернуть 0;\n"
        "  }\n"
        "}"
    ) == 1);
    
    // Условие ложно
    assert(run_code(
        "функция Начало() : целое {\n"
        "  целое a = 2;\n"
        "  если (a > 3) {\n"
        "    вернуть 1;\n"
        "  } иначе {\n"
        "    вернуть 0;\n"
        "  }\n"
        "}"
    ) == 0);
    
    // Вложенные if
    assert(run_code(
        "функция Начало() : целое {\n"
        "  если (истина) {\n"
        "    если (ложь) {\n"
        "      вернуть 0;\n"
        "    } иначе {\n"
        "      вернуть 99;\n"
        "    }\n"
        "  }\n"
        "  вернуть -1;\n"
        "}"
    ) == 99);
    
    std::cout << "[OK] Ветвления (if-else)\n";
}

void test_context_isolation() {
    // Изоляция переменных в блоках
    assert(run_code(
        "функция Начало() : целое {\n"
        "  целое x = 10;\n"
        "  {\n"
        "    целое x = 20;\n"
        "  }\n"
        "  вернуть x;\n"
        "}"
    ) == 10);
    
    // Чтение переменной из внешнего скоупа
    assert(run_code(
        "функция Начало() : целое {\n"
        "  целое y = 5;\n"
        "  {\n"
        "    y = 15;\n"
        "  }\n"
        "  вернуть y;\n"
        "}"
    ) == 15);
    
    std::cout << "[OK] Изоляция контекста (scoping)\n";
}

void test_panics() {
    std::cout << "[INFO] Ожидается ошибка времени выполнения (деление на ноль)...\n";
    std::cout << "       ---> ";
    std::flush(std::cout);

    pid_t pid = fork();
    if (pid == 0) {
        run_code("функция Начало() : целое { вернуть 10 / 0; }");
        std::exit(0);
    } else {
        int status;
        waitpid(pid, &status, 0);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 1 && "Интерпретатор НЕ вызвал panic при делении на ноль!");
        std::cout << "[OK] Ошибки времени выполнения (panic)\n";
    }
}

int main() {
    std::cout << "=== Запуск тестов Interpreter ===\n";
    
    test_ast_execution_and_returns();
    test_mathematics();
    test_branching();
    test_context_isolation();
    test_panics();
    
    std::cout << "=== Все тесты интерпретатора успешно пройдены! ===\n";
    return 0;
}
