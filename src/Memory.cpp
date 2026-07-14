#include "Memory.hpp"
#include <cstdlib>
#include <iostream>

namespace Memory {

Arena::Arena() { allocate_new_chunk(); }

void Arena::allocate_new_chunk() {
  try {
    // Выделяем новый блок памяти
    auto chunk = std::make_unique<std::byte[]>(CHUNK_SIZE);
    current_ptr = chunk.get();
    space_left = CHUNK_SIZE;
    chunks.push_back(std::move(chunk));
  } catch (const std::bad_alloc &) {
    panic("Критическая ошибка: Недостаточно памяти для выделения чанка арены "
          "(OOM).");
  }
}

void *Arena::alloc(size_t size, size_t alignment) {
  if (size == 0)
    return nullptr;

  // Проверка на превышение размера чанка
  if (size > CHUNK_SIZE) {
    panic("Критическая ошибка: Запрос на выделение памяти превышает размер "
          "чанка (4 МБ).");
  }

  // Вычисляем смещение для выравнивания
  uintptr_t raw_addr = reinterpret_cast<uintptr_t>(current_ptr);
  uintptr_t aligned_addr = (raw_addr + (alignment - 1)) & ~(alignment - 1);
  size_t padding = aligned_addr - raw_addr;

  // Если места с учетом выравнивания не хватает, берем новый чанк
  if (size + padding > space_left) {
    allocate_new_chunk();
    // Пересчитываем выравнивание для нового чанка (обычно он уже выровнен по
    // максимуму)
    raw_addr = reinterpret_cast<uintptr_t>(current_ptr);
    aligned_addr = (raw_addr + (alignment - 1)) & ~(alignment - 1);
    padding = aligned_addr - raw_addr;
  }

  void *result = reinterpret_cast<void *>(aligned_addr);
  current_ptr = reinterpret_cast<std::byte *>(aligned_addr + size);
  space_left -= (size + padding);

  return result;
}

void Arena::reset() {
  chunks.clear();
  current_ptr = nullptr;
  space_left = 0;
  allocate_new_chunk();
}

void Arena::panic(const char *message) {
  std::cerr << message << std::endl;
  std::exit(1);
}

} // namespace Memory