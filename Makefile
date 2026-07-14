CXX = g++
OPTFLAGS ?= -O2
# -MMD -MP: генерируют .d файлы с зависимостями от заголовков, чтобы make пересобирал
# .o при изменении .hpp, а не только .cpp (без этого правка заголовка молча оставляет
# другие .o с устаревшим ABI/размером структур до следующего `make clean`).
CXXFLAGS = -std=c++23 -Wall -Wextra $(OPTFLAGS) -Iinc -MMD -MP

SRC_DIR = src
INC_DIR = inc
OBJ_DIR = obj
TEST_DIR = tests
TEST_BIN_DIR = $(OBJ_DIR)/tests

# Исходники и объекты (без main.cpp)
LIB_SOURCES = $(filter-out $(SRC_DIR)/main.cpp, $(wildcard $(SRC_DIR)/*.cpp))
LIB_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(LIB_SOURCES))

MAIN_OBJ = $(OBJ_DIR)/main.o
EXEC = myi

# Тесты (компилируем все файлы в tests/*.cpp)
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINS = $(patsubst $(TEST_DIR)/%.cpp, $(TEST_BIN_DIR)/%, $(TEST_SOURCES))

all: $(EXEC)

asan:
	$(MAKE) clean
	$(MAKE) all OPTFLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer"

test: all $(TEST_BINS)
	@echo "=== Запуск C++ тестов ==="
	@export ASAN_OPTIONS=exitcode=88:detect_leaks=1 && \
	for t in $(TEST_BINS); do ./$$t || exit 1; done
	@echo "=== Запуск скриптовых тестов ==="
	./run_tests.sh

test-asan:
	$(MAKE) clean
	$(MAKE) test OPTFLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer"

# Линковка основного интерпретатора
$(EXEC): $(LIB_OBJECTS) $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Компиляция тестовых бинарников
$(TEST_BIN_DIR)/%: $(TEST_DIR)/%.cpp $(LIB_OBJECTS) | $(TEST_BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB_OBJECTS)

# Компиляция объектных файлов
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Создание папок
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

clean:
	rm -rf $(OBJ_DIR) $(EXEC)

.PHONY: all clean asan test test-asan

-include $(LIB_OBJECTS:.o=.d) $(MAIN_OBJ:.o=.d)