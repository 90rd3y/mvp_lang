#include "../inc/Parser.hpp"
#include "../inc/Lexer.hpp"
#include "../inc/Memory.hpp"
#include <iostream>
#include <cassert>
#include <unistd.h>
#include <sys/wait.h>

using namespace Parser;

struct ASTContext {
    Memory::Arena arena;
    Lexer::StringPool pool;
    std::vector<Lexer::Token> tokens;
    std::unique_ptr<Parser::Parser> parser;
    
    ASTContext(std::string_view source) : pool(arena) {
        Lexer::Scanner scanner(source, pool);
        tokens = scanner.tokenize();
        parser = std::make_unique<Parser::Parser>(tokens, pool);
    }
    
    NodeId parse() {
        return parser->parse();
    }
    
    const ASTNode& get_node(NodeId id) {
        return parser->get_nodes()[id];
    }
    
    NodeId get_child(NodeId id, uint32_t index) {
        const auto& node = get_node(id);
        assert(index < node.children_count && "Child index out of bounds");
        return parser->get_child_indices()[node.children_offset + index];
    }
};

void test_math_precedence() {
    // 1 + 2 * 3 должно парситься как 1 + (2 * 3)
    ASTContext ctx("целое a = 1 + 2 * 3;");
    NodeId root = ctx.parse();
    
    const ASTNode& prog = ctx.get_node(root);
    assert(prog.type == NodeType::Program);
    assert(prog.children_count == 1);
    
    NodeId decl_id = ctx.get_child(root, 0);
    const ASTNode& decl = ctx.get_node(decl_id);
    assert(decl.type == NodeType::VarDecl);
    assert(decl.children_count == 2); // Инициализатор и тип
    
    NodeId plus_id = ctx.get_child(decl_id, 0);
    const ASTNode& plus_node = ctx.get_node(plus_id);
    assert(plus_node.type == NodeType::BinaryOp);
    assert(plus_node.token.type == Lexer::TokenType::Plus);
    assert(plus_node.children_count == 2);
    
    NodeId left_id = ctx.get_child(plus_id, 0);
    const ASTNode& left_node = ctx.get_node(left_id);
    assert(left_node.type == NodeType::LiteralInt);
    assert(left_node.token.lexeme == "1");
    
    NodeId mul_id = ctx.get_child(plus_id, 1);
    const ASTNode& mul_node = ctx.get_node(mul_id);
    assert(mul_node.type == NodeType::BinaryOp);
    assert(mul_node.token.type == Lexer::TokenType::Star);
    assert(mul_node.children_count == 2);
    
    NodeId mul_left_id = ctx.get_child(mul_id, 0);
    const ASTNode& mul_left = ctx.get_node(mul_left_id);
    assert(mul_left.type == NodeType::LiteralInt);
    assert(mul_left.token.lexeme == "2");
    
    NodeId mul_right_id = ctx.get_child(mul_id, 1);
    const ASTNode& mul_right = ctx.get_node(mul_right_id);
    assert(mul_right.type == NodeType::LiteralInt);
    assert(mul_right.token.lexeme == "3");
    
    std::cout << "[OK] Приоритет математических операций\n";
}

void test_nested_blocks() {
    ASTContext ctx("{ { целое x = 1; } }");
    NodeId root = ctx.parse();
    
    const ASTNode& prog = ctx.get_node(root);
    assert(prog.type == NodeType::Program);
    
    NodeId block1_id = ctx.get_child(root, 0);
    const ASTNode& block1 = ctx.get_node(block1_id);
    assert(block1.type == NodeType::Block);
    assert(block1.children_count == 1);
    
    NodeId block2_id = ctx.get_child(block1_id, 0);
    const ASTNode& block2 = ctx.get_node(block2_id);
    assert(block2.type == NodeType::Block);
    assert(block2.children_count == 1);
    
    NodeId decl_id = ctx.get_child(block2_id, 0);
    const ASTNode& decl = ctx.get_node(decl_id);
    assert(decl.type == NodeType::VarDecl);
    
    std::cout << "[OK] Вложенные блоки\n";
}

void test_syntax_error() {
    std::cout << "[INFO] Ожидается сообщение об ошибке парсера...\n";
    std::cout << "       ---> ";
    std::flush(std::cout);

    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        ASTContext ctx("целое 123 = ;");
        ctx.parse(); // Должно вызвать std::exit(1) через error()
        std::exit(0); // Если мы тут, значит ошибка
    } else {
        int status;
        waitpid(pid, &status, 0);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 1 && "Парсер НЕ вызвал exit(1) при синтаксической ошибке!");
        std::cout << "[OK] Обработка синтаксических ошибок (exit)\n";
    }
}

int main() {
    std::cout << "=== Запуск тестов Parser ===\n";
    
    test_math_precedence();
    test_nested_blocks();
    test_syntax_error();
    
    std::cout << "=== Все тесты парсера успешно пройдены! ===\n";
    return 0;
}
