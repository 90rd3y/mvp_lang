#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../inc/Interpreter.hpp"
#include "../inc/Lexer.hpp"
#include "../inc/Memory.hpp"
#include "../inc/Parser.hpp"
#include "../inc/Semantic.hpp"

// Вспомогательная функция для вывода дерева (для флага --dump-ast)
void print_ast(const std::vector<Parser::ASTNode> &nodes,
               const std::vector<Parser::NodeId> &indices,
               Parser::NodeId node_id, int depth = 0) {
  if (node_id == Parser::InvalidNode)
    return;
  const auto &node = nodes[node_id];

  for (int i = 0; i < depth; ++i)
    std::cout << "  ";

  std::cout << "Node[" << node_id << "] ";
  switch (node.type) {
  case Parser::NodeType::Program:
    std::cout << "Program";
    break;
  case Parser::NodeType::Block:
    std::cout << "Block";
    break;
  case Parser::NodeType::VarDecl:
    std::cout << "VarDecl" << (node.is_mutable ? "" : " (const)");
    break;
  case Parser::NodeType::AssignStmt:
    std::cout << "AssignStmt";
    break;
  case Parser::NodeType::BinaryOp:
    std::cout << "BinaryOp (" << node.token.lexeme << ")";
    break;
  case Parser::NodeType::LiteralInt:
    std::cout << "LiteralInt (" << node.token.lexeme << ")";
    break;
  case Parser::NodeType::LiteralFloat:
    std::cout << "LiteralFloat (" << node.token.lexeme << ")";
    break;
  case Parser::NodeType::Identifier:
    std::cout << "Identifier (" << node.token.lexeme << ")";
    break;
  case Parser::NodeType::ExprStmt:
    std::cout << "ExprStmt";
    break;
  default:
    std::cout << "UnknownType";
    break;
  }
  std::cout << "\n";

  for (uint32_t i = 0; i < node.children_count; ++i) {
    print_ast(nodes, indices, indices[node.children_offset + i], depth + 1);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Использование: " << argv[0]
              << " <source_file> [--dump-tokens] [--dump-ast]" << std::endl;
    return 1;
  }

  std::string file_path;
  bool dump_tokens = false;
  bool dump_ast = false;

  // Простейший разбор аргументов
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dump-tokens") == 0)
      dump_tokens = true;
    else if (std::strcmp(argv[i], "--dump-ast") == 0)
      dump_ast = true;
    else
      file_path = argv[i];
  }

  if (file_path.empty()) {
    std::cerr << "Ошибка: не указан файл исходного кода." << std::endl;
    return 1;
  }

  // Чтение файла
  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "Ошибка: невозможно открыть файл " << file_path << std::endl;
    return 1;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source_code = buffer.str();

  // === ФАЗА 1: Инициализация и Лексический анализ ===
  Memory::Arena
      ast_arena; // Арена для интернирования строк и типов на этапе статического анализа
  Lexer::StringPool pool(ast_arena);

  Lexer::Scanner scanner(source_code, pool);
  std::vector<Lexer::Token> tokens = scanner.tokenize();

  if (dump_tokens) {
    for (const auto &t : tokens) {
      std::cout << "Token[" << t.line << ":" << t.column
                << "] Type: " << static_cast<int>(t.type) << " Lexeme: '"
                << t.lexeme << "'\n";
    }
    return 0; // Завершаем работу, как требует ТЗ
  }

  // === ФАЗА 2: Синтаксический анализ ===
  Parser::Parser parser(tokens, pool);
  Parser::NodeId root = parser.parse();

  if (dump_ast) {
    print_ast(parser.get_nodes(), parser.get_child_indices(), root);
    return 0;
  }

  // === ФАЗА 3: Семантический анализ ===
  Semantic::Analyzer semantic(parser.get_nodes(), parser.get_child_indices(),
                              pool);
  semantic.analyze(root);

  // === ФАЗА 4: Выполнение (Runtime) ===
  Memory::Arena runtime_arena; // Отдельная арена для динамических данных во
                               // время выполнения (например строк)
  Interpreter::VM vm(parser.get_nodes(), parser.get_child_indices(), pool,
                     runtime_arena);

  return vm.run(root);
}