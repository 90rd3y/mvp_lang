# Грамматика Языка

Этот документ описывает лексическую структуру и синтаксическую грамматику учебного языка. Для описания используется расширенная форма Бэкуса-Наура (EBNF).

## 1. Лексическая структура (Lexer)

### 1.1 Токены
Исходный код программы разбивается на последовательность токенов:
- **Ключевые слова**: зарезервированные слова, определяющие конструкции языка (`целое`, `функция`, `если`, `пока` и т.д.).
- **Идентификаторы**: имена переменных, функций, структур и типов.
- **Литералы**: числовые, строковые и логические константы.
- **Операторы и пунктуация**: математические символы (`+`, `-`), логические операторы (`&&`, `||`), разделители (`;`, `,`, `(`, `)`, `{`, `}`).

### 1.2 Алфавит и правила наименования
* Идентификаторы состоят из кириллических или латинских букв, цифр и символа подчеркивания `_`.
* Идентификатор не может начинаться с цифры.
* Язык регистрозависимый (case-sensitive).

```ebnf
Identifier ::= [a-zA-Zа-яА-Я_] [a-zA-Zа-яА-Я0-9_]*
```

### 1.3 Литералы
```ebnf
IntLiteral    ::= [0-9]+
FloatLiteral  ::= [0-9]+ "." [0-9]+
StringLiteral ::= '"' [^"]* '"'
BoolLiteral   ::= "истина" | "ложь"
```

---

## 2. Синтаксическая грамматика (Parser)

Грамматика основана на рекурсивном спуске для объявлений и операторов (statements) и на Pratt-парсере для математических выражений (expressions).

### 2.1 Структура программы
Программа состоит из набора объявлений на глобальном уровне.
```ebnf
Program ::= Decl* EOF
```

### 2.2 Объявления (Declarations)
```ebnf
Decl ::= FuncDecl
       | StructDecl
       | TypeAlias
       | VarDecl
       | Stmt

FuncDecl   ::= "функция" Identifier "(" (Params)? ")" ":" Identifier Block
Params     ::= Param ("," Param)*
Param      ::= Identifier Identifier /* Тип и Имя */

StructDecl ::= "структура" Identifier "{" (Identifier Identifier ";")* "}"

TypeAlias  ::= "тип" Identifier "=" Identifier ";"

VarDecl    ::= Identifier ("[" IntLiteral "]")? Identifier ("=" Expr)? ";"
```

### 2.3 Инструкции (Statements)
```ebnf
Stmt ::= IfStmt 
       | WhileStmt 
       | ReturnStmt 
       | BreakStmt 
       | ContinueStmt 
       | Block 
       | ExprStmt

IfStmt       ::= "если" "(" Expr ")" Stmt ("иначе" Stmt)?
WhileStmt    ::= "пока" "(" Expr ")" Stmt
ReturnStmt   ::= "вернуть" Expr? ";"
BreakStmt    ::= "прервать" ";"
ContinueStmt ::= "продолжить" ";"
Block        ::= "{" Decl* "}"
ExprStmt     ::= Expr ";"
```

### 2.4 Встроенные функции
Интерпретатор предоставляет встроенные языковые конструкции, которые ведут себя подобно ключевым словам:
```ebnf
BuiltinCall ::= "печать" Expr 
              | "ввод" "(" ")" 
              | "выход" "(" Expr ")" 
              | "паника" "(" Expr ")" 
              | "утверждение" "(" Expr ")"
```

### 2.5 Выражения (Expressions - Pratt Parser)
Выражения парсятся с учетом приоритетов (от самого низкого к самому высокому):

1. **Присваивание**: `a = b` (правоассоциативное)
2. **Логическое ИЛИ**: `||`
3. **Логическое И**: `&&`
4. **Равенство**: `==`, `!=`
5. **Сравнение**: `<`, `<=`, `>`, `>=`
6. **Сложение и вычитание**: `+`, `-`
7. **Умножение, деление и остаток**: `*`, `/`, `%`
8. **Унарные операции**: `-x`, `!x`
9. **Постфиксные операции**:
   * Вызов функции: `f(x)`
   * Индексация массива: `arr[i]`
   * Доступ к полю: `obj.field`

```ebnf
Expr         ::= Assignment

Assignment   ::= LogicOr ("=" Expr)?
LogicOr      ::= LogicAnd ("||" LogicAnd)*
LogicAnd     ::= Equality ("&&" Equality)*
Equality     ::= Comparison (("==" | "!=") Comparison)*
Comparison   ::= Term (("<" | "<=" | ">" | ">=") Term)*
Term         ::= Factor (("+" | "-") Factor)*
Factor       ::= Unary (("*" | "/") Unary)*

Unary        ::= ("-" | "!") Unary | Postfix
Postfix      ::= Primary 
               | Postfix "(" Arguments? ")" 
               | Postfix "[" Expr "]" 
               | Postfix "." Identifier

Primary      ::= Identifier 
               | IntLiteral 
               | FloatLiteral 
               | StringLiteral 
               | BoolLiteral 
               | BuiltinCall
               | "(" Expr ")"

Arguments    ::= Expr ("," Expr)*
```
