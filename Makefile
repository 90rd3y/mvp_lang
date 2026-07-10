CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -Iinc

# Директории
SRC_DIR = src
INC_DIR = inc
OBJ_DIR = obj

# Исходники и объекты
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SOURCES))

# Имя исполняемого файла (согласно ТЗ)
EXEC = myi

all: $(EXEC)

# Линковка
$(EXEC): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Компиляция объектных файлов
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Создание папки для объектов
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(EXEC)

.PHONY: all clean