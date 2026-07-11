import re
with open("src/Interpreter.cpp", "r") as f:
    c = f.read()

literals_code = """    case Parser::NodeType::ArrayLiteral: {
        Value val;
        val.kind = Semantic::TypeKind::Array;
        val.as.arr = static_cast<Value*>(arena.alloc(sizeof(Value) * node.children_count, alignof(Value)));
        for (uint32_t i = 0; i < node.children_count; ++i) {
            val.as.arr[i] = eval(child_indices[node.children_offset + i]);
        }
        return val;
    }
    case Parser::NodeType::StructLiteral: {
        Value val;
        val.kind = Semantic::TypeKind::Struct;
        val.as.arr = static_cast<Value*>(arena.alloc(sizeof(Value) * node.children_count, alignof(Value)));
        for (uint32_t i = 0; i < node.children_count; ++i) {
            val.as.arr[i] = eval(child_indices[node.children_offset + i]);
        }
        return val;
    }
    case Parser::NodeType::Identifier:"""

c = c.replace("case Parser::NodeType::Identifier:", literals_code)

with open("src/Interpreter.cpp", "w") as f:
    f.write(c)
