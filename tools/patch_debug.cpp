--- src/Interpreter.cpp
+++ src/Interpreter.cpp
@@ -152,6 +152,7 @@
 
       for (uint32_t i = 0; i < count; ++i) {
           v.as.arr[i] = eval(child_indices[node.children_offset + offset_idx + i]);
+          std::cerr << "INIT ARRAY[" << i << "] = kind:" << (int)v.as.arr[i].kind << " val:" << v.as.arr[i].as.i64 << std::endl;
       }
       return v;
   }
