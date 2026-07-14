#!/bin/bash

# Цвета для красивого вывода
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

export ASAN_OPTIONS=exitcode=88:detect_leaks=1

INTERPRETER="./myi"
PASSED=0
FAILED=0

echo "=== Запуск тестов интерпретатора ==="

# 1. Проверяем успешные тесты
echo -e "\n[ Проверка корректного кода (должен вернуть 0) ]"
for file in tests/pass/*.lang; do
    # Сохраняем вывод во временный файл для точного сравнения
    TEMP_OUT=".temp_stdout.out"
    $INTERPRETER "$file" > "$TEMP_OUT" 2>/dev/null
    EXIT_CODE=$?
    
    OUT_FILE="${file%.lang}.out"
    
    if [ $EXIT_CODE -eq 88 ]; then
        echo -e "${RED}[ASAN FAIL]${NC} $file (Утечка или ошибка памяти)"
        ((FAILED++))
    elif [ $EXIT_CODE -eq 0 ]; then
        if [ -f "$OUT_FILE" ]; then
            # Сравниваем вывод с помощью diff (игнорируем разницу в переводах строк \r)
            DIFF_OUTPUT=$(diff -u --strip-trailing-cr "$OUT_FILE" "$TEMP_OUT" 2>&1)
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}[OK]${NC} $file"
                ((PASSED++))
            else
                echo -e "${RED}[FAIL]${NC} $file (вывод не совпадает)"
                # Выводим разницу, сдвигая её вправо для красоты
                echo "$DIFF_OUTPUT" | sed 's/^/  /'
                ((FAILED++))
            fi
        else
            echo -e "${GREEN}[OK]${NC} $file (нет файла .out, проверен только код 0)"
            ((PASSED++))
        fi
    else
        echo -e "${RED}[FAIL]${NC} $file (Код возврата: $EXIT_CODE)"
        ((FAILED++))
    fi
done
rm -f .temp_stdout.out

# 2. Проверяем тесты, которые должны упасть
echo -e "\n[ Проверка обработки ошибок (должен вернуть != 0) ]"
for file in tests/fail/*.lang; do
    # Читаем ожидаемую ошибку из первой строки
    EXPECTED_ERR=$(head -n 1 "$file" | grep -oP "// ОЖИДАЕТСЯ ОШИБКА: \K.*" || true)
    
    # Запускаем интерпретатор, захватываем stderr (stdout отправляем в /dev/null)
    STDERR=$($INTERPRETER "$file" 2>&1 >/dev/null)
    EXIT_CODE=$?
    
    if [ $EXIT_CODE -eq 88 ]; then
        echo -e "${RED}[ASAN FAIL]${NC} $file (Утечка или ошибка памяти)"
        ((FAILED++))
    elif [ $EXIT_CODE -ne 0 ]; then
        if [ -n "$EXPECTED_ERR" ]; then
            # Убираем пробелы (trim) средствами bash, чтобы не потерять кавычки
            EXPECTED_CLEAN="${EXPECTED_ERR#"${EXPECTED_ERR%%[![:space:]]*}"}"
            EXPECTED_CLEAN="${EXPECTED_CLEAN%"${EXPECTED_CLEAN##*[![:space:]]}"}"
            
            # Проверяем вхождение строки без учета регистра (поддерживает кириллицу)
            if echo "$STDERR" | grep -q -i -F "$EXPECTED_CLEAN"; then
                echo -e "${GREEN}[OK]${NC} $file упал с ожидаемой ошибкой"
                ((PASSED++))
            else
                echo -e "${RED}[FAIL]${NC} $file упал, но вывод ошибки не совпадает"
                echo -e "  Ожидалось: $EXPECTED_CLEAN"
                echo -e "  Получено:  $(echo "$STDERR" | head -n 1)"
                ((FAILED++))
            fi
        else
            echo -e "${GREEN}[OK]${NC} $file упал как и ожидалось (ожидаемая ошибка не задана)"
            ((PASSED++))
        fi
    else
        echo -e "${RED}[FAIL]${NC} $file не выдал ошибку!"
        ((FAILED++))
    fi
done

# 3. Проверка аргументов CLI
echo -e "\n[ Проверка обработки некорректных аргументов CLI ]"

# 3.1 Без аргументов
STDERR=$($INTERPRETER 2>&1 >/dev/null)
if [[ "$STDERR" == *"Использование:"* ]]; then
    echo -e "${GREEN}[OK]${NC} Запуск без аргументов обработан корректно"
    ((PASSED++))
else
    echo -e "${RED}[FAIL]${NC} Запуск без аргументов не выдал 'Использование:'"
    ((FAILED++))
fi

# 3.2 Несуществующий файл
STDERR=$($INTERPRETER "tests/fail/non_existent_file_123.lang" 2>&1 >/dev/null)
if [[ "$STDERR" == *"невозможно открыть файл"* ]]; then
    echo -e "${GREEN}[OK]${NC} Запуск с несуществующим файлом обработан корректно"
    ((PASSED++))
else
    echo -e "${RED}[FAIL]${NC} Запуск с несуществующим файлом не отловил ошибку"
    ((FAILED++))
fi

# 3.3 Только неверный флаг (система решит, что это имя файла)
STDERR=$($INTERPRETER "--invalid-flag" 2>&1 >/dev/null)
if [[ "$STDERR" == *"невозможно открыть файл"* ]]; then
    echo -e "${GREEN}[OK]${NC} Запуск с неверным флагом обработан корректно"
    ((PASSED++))
else
    echo -e "${RED}[FAIL]${NC} Запуск с неверным флагом не отловил ошибку"
    ((FAILED++))
fi

# 3.4 Пустой файл
touch tests/fail/.temp_empty.lang
STDERR=$($INTERPRETER "tests/fail/.temp_empty.lang" 2>&1 >/dev/null)
if echo "$STDERR" | grep -q -i -F "точка входа 'начало' не найдена"; then
    echo -e "${GREEN}[OK]${NC} Запуск с пустым файлом обработан корректно"
    ((PASSED++))
else
    echo -e "${RED}[FAIL]${NC} Запуск с пустым файлом не отловил отсутствие 'Начало'"
    ((FAILED++))
fi
rm -f tests/fail/.temp_empty.lang

echo -e "\n=== Итоги ==="
echo -e "Успешно: ${GREEN}$PASSED${NC}"
echo -e "Упало:   ${RED}$FAILED${NC}"

if [ $FAILED -ne 0 ]; then
    exit 1
else
    exit 0
fi
