#!/bin/bash

# Цвета для красивого вывода
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

INTERPRETER="./myi"
PASSED=0
FAILED=0

echo "=== Запуск тестов интерпретатора ==="

# 1. Проверяем успешные тесты
echo -e "\n[ Проверка корректного кода (должен вернуть 0) ]"
for file in tests/pass/*.lang; do
    $INTERPRETER "$file" > /dev/null 2>&1
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 0 ]; then
        echo -e "${GREEN}[OK]${NC} $file"
        ((PASSED++))
    else
        echo -e "${RED}[FAIL]${NC} $file (Код возврата: $EXIT_CODE)"
        ((FAILED++))
    fi
done

# 2. Проверяем тесты, которые должны упасть
echo -e "\n[ Проверка обработки ошибок (должен вернуть != 0) ]"
for file in tests/fail/*.lang; do
    $INTERPRETER "$file" > /dev/null 2>&1
    EXIT_CODE=$?
    if [ $EXIT_CODE -ne 0 ]; then
        echo -e "${GREEN}[OK]${NC} $file упал как и ожидалось"
        ((PASSED++))
    else
        echo -e "${RED}[FAIL]${NC} $file не выдал ошибку!"
        ((FAILED++))
    fi
done

echo -e "\n=== Итоги ==="
echo -e "Успешно: ${GREEN}$PASSED${NC}"
echo -e "Упало:   ${RED}$FAILED${NC}"

if [ $FAILED -ne 0 ]; then
    exit 1
else
    exit 0
fi
