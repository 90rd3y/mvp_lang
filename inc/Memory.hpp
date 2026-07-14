#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace Memory {

// Арена-аллокатор (Bump Allocator).
// Выделяет память блоками (чанками) по 4 МБ.
// Индивидуальное освобождение объектов невозможно.
class Arena {
public:
    // Размер чанка строго 4 МБ
    static constexpr size_t CHUNK_SIZE = 4 * 1024 * 1024;
    // Выравнивание по умолчанию 8 байт
    static constexpr size_t DEFAULT_ALIGNMENT = 8;

    Arena();
    ~Arena() = default;

    // Запрещаем копирование арены
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    // Выделяет блок памяти заданного размера и выравнивания.
    // size - размер в байтах
    // alignment - выравнивание (должно быть степенью двойки)
    void* alloc(size_t size, size_t alignment = DEFAULT_ALIGNMENT);

    // Вспомогательный метод для конструирования объекта типа T в арене.
    template<typename T, typename... Args>
    T* construct(Args&&... args) {
        void* mem = alloc(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    // Сброс всей памяти (освобождение всех чанков).
    void reset();

private:
    // Список чанков памяти. unique_ptr гарантирует очистку при уничтожении вектора.
    std::vector<std::unique_ptr<std::byte[]>> chunks;
    
    std::byte* current_ptr = nullptr; // Указатель на свободное место в текущем чанке
    size_t space_left = 0;           // Оставшееся место в текущем чанке

    void allocate_new_chunk();
    void panic(const char* message);
};

} // namespace Memory