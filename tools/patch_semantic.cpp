--- src/Semantic.cpp
+++ src/Semantic.cpp
@@ -326,14 +326,30 @@
             result = type_table.get_builtin(TypeKind::Void);
             break;
         }
-                }
-                return id;
-            };
-
-            if (resolve_alias(ret_type) != resolve_alias(current_func_return_type)) {
-                error(node.token, "Тип возвращаемого значения не совпадает с сигнатурой функции");
-            }
-            result = ret_type;
-            break;
+        case Parser::NodeType::Call: {
+            Parser::NodeId callee_id = child_indices[node.children_offset];
+            Lexer::IdentId func_name = nodes[callee_id].token.data;
+            std::string fname_str = std::string(pool.get(func_name));
+
+            if (fname_str == "печать" || fname_str == "ввод" || fname_str == "выход" || fname_str == "паника" || fname_str == "утверждение") {
+                for (uint32_t i = 1; i < node.children_count; ++i) {
+                    check(child_indices[node.children_offset + i]);
+                }
+                if (fname_str == "ввод") result = type_table.get_builtin(TypeKind::String);
+                else result = type_table.get_builtin(TypeKind::Void);
+                break;
+            }
+
+            if (!functions.count(func_name)) error(node.token, "Вызов неизвестной функции");
+            auto& sig = functions[func_name];
+            if (node.children_count - 1 != sig.param_types.size()) {
+                error(node.token, "Неверное количество аргументов");
+            }
+            for (uint32_t i = 1; i < node.children_count; ++i) {
+                TypeId arg_type = check(child_indices[node.children_offset + i]);
+                if (!types_compatible(arg_type, sig.param_types[i - 1])) error(node.token, "Несоответствие типа аргумента");
+            }
+            result = sig.return_type;
+            break;
+        }
+        case Parser::NodeType::Return: {
+            TypeId ret_type = type_table.get_builtin(TypeKind::Void);
+            if (node.children_count > 0) {
+                ret_type = check(child_indices[node.children_offset]);
+            }
+            if (!types_compatible(ret_type, current_func_return_type)) {
+                error(node.token, "Тип возвращаемого значения не совпадает с сигнатурой функции");
+            }
+            result = ret_type;
+            break;
         }
     default:
