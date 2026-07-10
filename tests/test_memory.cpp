#include "../inc/Memory.hpp"
#include <iostream>
#include <cassert>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdint>

void test_zero_allocation() {
    Memory::Arena arena;
    void* ptr = arena.alloc(0);
    assert(ptr == nullptr && "Запрос 0 байт должен возвращать nullptr");
    std::cout << "[OK] Граничный: Выделение 0 байт\n";
}

void test_large_alignment() {
    Memory::Arena arena;
    // Запрашиваем 1 байт, но с огромным выравниванием (4096 байт - размер страницы)
    void* ptr = arena.alloc(1, 4096);
    assert(reinterpret_cast<uintptr_t>(ptr) % 4096 == 0 && "Память не выровнена по границе 4096");
    std::cout << "[OK] Граничный: Нестандартное выравнивание (4096)\n";
}

void test_stress_multiple_chunks() {
    Memory::Arena arena;
    // Размер чанка 4 МБ. Выделяем 1 байт (с выравниванием до 8) 1 000 000 раз.
    // Это потребует ~8 МБ памяти, то есть арена должна незаметно выделить минимум 2 чанка.
    for (int i = 0; i < 1000000; ++i) {
        void* ptr = arena.alloc(1, 8);
        assert(ptr != nullptr && "Арена вернула nullptr при стресс-тесте");
    }
    std::cout << "[OK] Стресс: Выделение 1 млн мелких объектов (переключение чанков)\n";
}

void test_reset_behavior() {
    Memory::Arena arena;
    arena.alloc(1024 * 1024); // Выделяем 1 МБ
    arena.reset();            // Сбрасываем (память должна очиститься, создаться новый пустой чанк)
    void* ptr = arena.alloc(4 * 1024 * 1024 - 8); // Выделяем почти весь чанк
    assert(ptr != nullptr && "Сброс арены отработал некорректно");
    std::cout << "[OK] Обычный: Сброс арены (reset)\n";
}

// Негативный тест: Запрос памяти больше размера чанка (4 МБ)
void test_oversized_allocation() {
    std::cout << "[INFO] Ожидается вывод сообщения о критической ошибке (OOM/Oversize)...\n";
    std::cout << "       ---> ";
    std::flush(std::cout);

    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс: пытаемся сломать аллокатор
        Memory::Arena arena;
        arena.alloc(5 * 1024 * 1024); // 5 МБ (больше CHUNK_SIZE)
        std::exit(0); // Если мы дошли сюда, значит panic не сработал — это ошибка теста!
    } else {
        // Родительский процесс: ждем завершения дочернего
        int status;
        waitpid(pid, &status, 0);
        // Проверяем, что процесс завершился аварийно с кодом 1 (как прописано в Memory::panic)
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 1 && "Арена НЕ вызвала panic при превышении размера!");
        std::cout << "[OK] Негативный: Запрос > 4 МБ корректно вызывает panic\n";
    }
}

int main() {
    std::cout << "=== Запуск тестов Memory::Arena ===\n";
    
    test_zero_allocation();
    test_large_alignment();
    test_stress_multiple_chunks();
    test_reset_behavior();
    test_oversized_allocation();
    
    std::cout << "=== Все тесты памяти успешно пройдены! ===\n";
    return 0;
}
