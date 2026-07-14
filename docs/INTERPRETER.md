# Модуль интерпретации: устройство и механизмы

Документ описывает `inc/Interpreter.hpp` и `src/Interpreter.cpp` — рантайм-часть языка (`VM`, `Environment`, `Value`). Все диаграммы построены по фактическому коду и отражают текущее (актуальное) поведение.

---

## 1. Место интерпретатора в конвейере

`VM` — последняя из четырёх фаз `main.cpp`. Она получает от парсера то же самое плоское AST (`nodes` + `child_indices`), что и видел `Analyzer`, но не имеет доступа к результатам типового анализа (`node_resolved_types`) — заново выводит всё нужное из токенов узлов по ходу выполнения.

```mermaid
flowchart LR
    SRC["Исходный текст (.lang)"] --> LEX["Lexer::Scanner\n(токены)"]
    LEX --> PARSE["Parser::Parser\n(плоский AST:\nnodes + child_indices)"]
    PARSE --> SEM["Semantic::Analyzer\n(статическая проверка типов,\nexit(1) при ошибке)"]
    SEM -->|"AST не изменяется,\nтолько проверяется"| VM["Interpreter::VM\n(рантайм)"]
    VM --> OUT["stdout / exit code /\nпаника в stderr"]

    style SEM fill:#fef3c7,stroke:#b45309
    style VM fill:#dbeafe,stroke:#1d4ed8
```

Ключевой инвариант архитектуры: **`Analyzer` и `VM` — два независимых обхода одного и того же дерева.** `VM` не читает кэш типов анализатора и не подозревает о существовании `Analyzer` — он просто доверяет, что если исполнение до него дошло, то программа статически корректна (типы согласованы, точка входа существует, арность встроенных функций верна и т.д.). Единственная связь между фазами — порядок вызова в `main.cpp`: `semantic.analyze(root)` завершает процесс через `exit(1)` при ошибке, не давая `vm.run(root)` вообще начаться.

---

## 2. Основные структуры данных

```mermaid
classDiagram
    class Value {
        +TypeKind kind
        +uint32_t count
        +union_as_i64_or_f64_or_b_or_s_or_arr as
    }

    class Environment {
        -vector~map~IdentId,Value~~ scopes
        +enter_scope()
        +exit_scope()
        +define(name, val)
        +assign(name, val)
        +get(name) Value
        +get_scopes() vector~map~
        +set_scopes(v)
    }

    class VM {
        -nodes
        -child_indices
        -Arena arena
        -Environment env
        -bool should_return
        -bool should_break
        -bool should_continue
        -Value return_value
        -map~IdentId,NodeId~ functions
        -vector~CallFrame~ call_stack
        -int current_line
        +run(root) int
        -eval(id) Value
        -execute(id) void
        -values_equal(a, b) bool
        -check_index_bounds(arr, idx) void
        -panic(msg) void
    }

    class CallFrame {
        +string func_name
        +int call_line
    }

    VM "1" *-- "1" Environment
    VM "1" *-- "*" CallFrame : call_stack
    Environment "1" o-- "*" Value : хранит по имени
    Value "1" o-- "*" Value : as.arr (Array/Struct)
```

### 2.1 `Value` — универсальное рантайм-значение

`Value` — это тегированное объединение (`kind` + `union as`) плюс поле `count`, добавленное поверх union'а:

```mermaid
flowchart TB
    subgraph V["Value (один слот в стеке/скоупе/массиве)"]
        K["kind: TypeKind\n(Int/Float/Bool/Char/String/Array/Struct/Void)"]
        C["count: uint32_t\n(только для Array/Struct)"]
        U["as: union { i64 | f64 | b | s | arr }"]
    end
    U -->|"Int, Char"| I64["i64"]
    U -->|"Float"| F64["f64"]
    U -->|"Bool"| B["b"]
    U -->|"String"| S["s → const char* в Арене"]
    U -->|"Array, Struct"| ARR["arr → Value* (первый элемент в Арене)"]
    ARR -.->|"count элементов, каждый — полноценный Value"| V
```

Важно: `Value` **не хранит свой статический `TypeId`** — только грубую категорию `kind`. Это осознанное упрощение: интерпретатору не нужен полный `TypeTable`, потому что статический анализ уже гарантировал корректность типов везде, где это важно. Но для массивов/структур одного `kind == Array` недостаточно, чтобы узнать размер — отсюда поле `count`: оно заполняется один раз при создании значения (`ArrayLiteral`/`StructLiteral`) и дальше просто копируется вместе с `as.arr` при каждом присваивании/передаче параметра, как единое "толстое значение" (указатель + длина). Именно `count` делает возможными проверку границ индекса (§7) и глубокое сравнение `==`/`!=` (§8) без обращения к типам вообще.

### 2.2 `Environment` — стек областей видимости

Это просто `std::vector<std::unordered_map<IdentId, Value>>`. Поиск переменной (`get`/`assign`) идёт от **последнего** элемента вектора (самая внутренняя область) к первому (самая внешняя = глобальная):

```mermaid
flowchart LR
    subgraph ENV["Environment::scopes (vector)"]
        direction LR
        S0["[0] глобальная\n(живёт всегда)"]
        S1["[1] тело функции\n/ параметры"]
        S2["[2] вложенный блок { }"]
    end
    LOOKUP["get(name) / assign(name, val)"] -->|"1. ищет здесь первым"| S2
    S2 -->|"2. не нашли → сюда"| S1
    S1 -->|"3. не нашли → сюда"| S0
    S0 -->|"4. не нашли вообще"| VOID["возвращает Value()\n(Void, тихо — не ошибка!)"]
```

`Environment::get` **никогда не бросает ошибку** на ненайденное имя — она просто не может произойти при штатной работе, так как `Analyzer` гарантировал, что все используемые идентификаторы объявлены. Отсутствие проверки здесь — не дефект, а сознательное доверие к статическому анализу (см. §1).

---

## 3. Два диспетчера: `eval` vs `execute`

У `VM` два взаимно рекурсивных метода, а не один универсальный `visit`. Это отражает разницу между **выражениями** (у них есть значение — `eval` возвращает `Value`) и **инструкциями** (у них есть эффект, но не значение — `execute` возвращает `void` и вместо этого выставляет флаги `should_return`/`should_break`/`should_continue`).

```mermaid
flowchart TD
    ROOT(["NodeId узла"]) --> Q{"Тип узла — выражение\nили инструкция?"}

    Q -->|"Литералы, Identifier,\nBinaryOp, UnaryOp, Call,\nIndexing, MemberAccess,\nAssignStmt, Cast,\nArrayLiteral, StructLiteral"| EVAL["VM::eval(id) → Value"]

    Q -->|"Program, Block,\nVarDecl, If, While,\nReturn, Break, Continue,\nFuncDecl, NamespaceDecl,\nStructDecl, TypeAlias"| EXEC["VM::execute(id) → void"]

    EVAL -->|"выражение содержит\nподвыражения"| EVAL
    EXEC -->|"составная инструкция\n(If/While/Block)\nвычисляет условие"| EVAL
    EXEC -->|"инструкция — обёртка\nвокруг выражения\n(ExprStmt, Call-как-инструкция,\nAssignStmt-как-инструкция)"| EVAL
    EVAL -.->|"default: неизвестный\nдля eval тип узла"| VOIDVAL["Value() — Void"]
    EXEC -.->|"default: неизвестный\nдля execute тип узла\n(на практике не встречается)"| EVAL

    style EVAL fill:#dbeafe,stroke:#1d4ed8
    style EXEC fill:#dcfce7,stroke:#15803d
```

Взаимная рекурсия видна в самом коде: `execute(If)` вызывает `eval(условие)`, а `eval(Call)` вызывает `execute(тело_функции)`. Это ожидаемо — язык позволяет вызывать функции внутри выражений, а функции состоят из инструкций.

### 3.1 Обход дерева и три "прерывающих" флага

`execute` не использует `return`/`break`/`continue` C++ для передачи управления вверх по стеку вызовов интерпретатора — вместо этого три булевых поля `VM` (`should_return`, `should_break`, `should_continue`) устанавливаются в точке инструкции и **проверяются на каждом уровне обхода**, пока не будут погашены тем узлом, которому они предназначены.

```mermaid
stateDiagram-v2
    [*] --> Running

    Running --> Running: execute(следующий\nстатement в Block/Program)

    Running --> ReturnSet: узел Return\n(should_return = true,\nreturn_value = ...)
    Running --> BreakSet: узел Break\n(should_break = true)
    Running --> ContinueSet: узел Continue\n(should_continue = true)

    ReturnSet --> ReturnSet: Block/Program видит флаг\nи останавливает свой цикл\n(флаг летит выше по стеку execute)
    ReturnSet --> Running: eval(Call) забирает\nreturn_value,\nсбрасывает should_return

    BreakSet --> Running: While видит флаг,\nсбрасывает should_break,\nвыходит из C++ while(true)
    ContinueSet --> Running: While видит флаг,\nсбрасывает should_continue,\nпереходит к след. итерации

    note right of ReturnSet
        should_return "прошивает" все
        промежуточные Block/If насквозь —
        погашается только на границе
        вызова функции (eval Call)
    end note
    note right of BreakSet
        should_break/should_continue
        погашаются на первом
        объемлющем While
    end note
```

Каждый вызов `execute(id)` начинается проверкой `if (should_return || should_break || should_continue) return;` — это и есть механизм "протекания" сигнала вверх без исключений и без явного возвращаемого значения у `execute`. `Block`/`Program` в цикле по своим детям после каждого `execute(child)` проверяют эти же три флага и обрывают собственный цикл, если сработал любой из них — именно так `вернуть`/`прервать`/`продолжить`, написанные глубоко внутри вложенных `{ }`, долетают до нужного уровня.

---

## 4. Жизненный цикл `VM::run`

```mermaid
sequenceDiagram
    participant Main as main.cpp
    participant VM
    participant Env as Environment
    participant Prog as execute(Program)

    Main->>VM: run(root)
    Note over VM: конструктор VM уже вызвал\nenv.enter_scope() — глобальная\nобласть создана и не будет\nзакрыта до конца программы
    VM->>Prog: execute(root)
    Note over Prog: Program НЕ вызывает\nenter_scope/exit_scope\n(в отличие от Block!)
    loop по детям Program
        Prog->>Prog: execute(FuncDecl) → functions[имя] = NodeId
        Prog->>Prog: execute(VarDecl) → env.define(...) в ГЛОБАЛЬНУЮ область
        Prog->>Prog: execute(NamespaceDecl) → рекурсивно, с префиксом
        Prog->>Prog: StructDecl/TypeAlias → нет эффекта в рантайме
    end
    VM->>VM: functions.count("Начало")?
    alt не найдена
        VM->>VM: panic("Точка входа 'Начало' не найдена")
        Note over VM: недостижимо при штатном порядке фаз —\nAnalyzer уже гарантировал наличие\n"Начало" статически (см. SEMANTICS.md §6)
    end
    VM->>VM: call_stack.push_back({"Начало", ...})
    VM->>Env: execute(тело "Начало")
    Note over Env: тело — Block, поэтому здесь\nоткрывается ЕЩЁ одна область\nповерх глобальной
    VM->>Main: return return_value.as.i64 (или 0)
```

После этого прохода `functions` — плоская таблица `IdentId → NodeId` (полное имя функции, включая префикс пространства имён, → узел её `FuncDecl`), а глобальная область `Environment` содержит все верхнеуровневые переменные. Это тот самый механизм, который делает глобальные переменные видимыми из любой функции (подробности — §5).

---

## 5. Область видимости во времени: почему глобальные переменные работают

Ниже — снимки стека `env.scopes` в разные моменты выполнения программы вида:

```text
целое счётчик = 100;             // глобальная переменная
функция помощник(): целое { вернуть счётчик + 1; }
функция Начало(): целое {
    целое x = 1;
    { целое y = 2; }
    помощник();
    вернуть 0;
}
```

```mermaid
flowchart TD
    subgraph T1["1 — конструктор VM"]
        A1["[0] {}"]
    end
    subgraph T2["2 — после execute(Program):\nVarDecl 'счётчик' выполнен"]
        A2["[0] {счётчик: 100}"]
    end
    subgraph T3["3 — внутри тела Начало (Block)"]
        A3_0["[0] {счётчик: 100}"]
        A3_1["[1] {x: 1}"]
    end
    subgraph T4["4 — внутри { целое y=2; } (вложенный Block)"]
        A4_0["[0] {счётчик: 100}"]
        A4_1["[1] {x: 1}"]
        A4_2["[2] {y: 2}"]
    end
    subgraph T5["5 — блок { } завершился\n(exit_scope), затем\nвызов помощник()"]
        A5_0["[0] {счётчик: 100}"]
        A5_1["[1] {} — новая, для помощник()"]
    end
    subgraph T6["6 — помощник() вернул(ась),\nвосстановлен стек Начало"]
        A6_0["[0] {счётчик: 100}"]
        A6_1["[1] {x: 1}"]
    end

    T1 --> T2 --> T3 --> T4 -->|"exit_scope()"| T5 -->|"env.set_scopes(old_scopes)"| T6
```

Момент **4 → 5** — самый нетривиальный: вызов `помощник()` (узел `Call` в `eval`) не расширяет текущий стек, а **полностью заменяет** его на `[копия scopes[0], новая пустая область]` — таким образом тело `помощник` не видит `x`/`y` вызывающего кода (нет замыканий), но видит глобальную область (индекс 0). После возврата исходный стек `[0]=счётчик, [1]=x` восстанавливается — но не буквально тем же объектом, а с одним нюансом, показанным в следующей диаграмме.

### 5.1 Почему запись в глобальную переменную внутри вызова не теряется

`Environment::get_scopes()`/`set_scopes()` работают **по значению** (копируют весь `vector`). Значит, вызываемая функция получает *копию* глобальной области, а не ссылку на неё — если её тело изменит глобальную переменную, изменится только копия. Чтобы это изменение всё же было видно после `return`, `eval(Call)` явно переносит копию обратно перед восстановлением остального стека вызывающего кода:

```mermaid
sequenceDiagram
    participant Caller as Вызывающий код
    participant EvalCall as eval(Call) для "помощник()"
    participant Env as Environment.scopes

    Caller->>EvalCall: eval(узел Call)
    EvalCall->>Env: old_scopes = get_scopes()
    Note over Env: old_scopes = [{счётчик:100}, {x:1}]
    EvalCall->>EvalCall: new_scopes = [old_scopes[0], {}]
    EvalCall->>Env: set_scopes(new_scopes)
    Note over Env: живой стек теперь\n[{счётчик:100}, {}] — копия!
    EvalCall->>EvalCall: bind параметры в new_scopes[1]
    EvalCall->>EvalCall: execute(тело помощник)
    Note over Env: если тело сделало\n"счётчик = счётчик + 1;",\nизменяется ТОЛЬКО живая копия
    EvalCall->>Env: get_scopes()[0]  (текущее, изменённое)
    EvalCall->>EvalCall: old_scopes[0] = это значение
    EvalCall->>Env: set_scopes(old_scopes)
    Note over Env: живой стек снова\n[{счётчик:101}, {x:1}] —\nизменение перенесено!
    EvalCall->>Caller: return ret_val
```

Без этого переноса (`old_scopes[0] = env.get_scopes()[0]` перед финальным `set_scopes`) любое присваивание глобальной переменной внутри функции, вызванной не напрямую из тела `Начало`, откатывалось бы сразу после `return`. Перенос происходит на **каждом** возврате из вызова, поэтому эффект накапливается корректно и через произвольную глубину вложенных вызовов (A зовёт B зовёт C; изменение C всплывает в B при возврате из C, затем в A при возврате из B).

---

## 6. Механизм вызова функции: полная схема

`eval(Call)` — самый длинный обработчик в `VM`. Он ветвится на два совершенно разных пути: встроенные функции (обрабатываются инлайн, без стека вызовов) и пользовательские функции (полный механизм с подменой областей видимости).

```mermaid
flowchart TD
    START(["eval(узел Call)"]) --> ISBUILTIN{"имя функции ∈\n{печать, ввод, выход,\nпаника, утверждение}?"}

    ISBUILTIN -->|да| EVALARGS_B["вычислить все аргументы\n(eval для каждого)"]
    EVALARGS_B --> WHICH{"какая именно?"}
    WHICH -->|печать| PRINT["вывести args[0] по kind\n+ '\\n', return Value()"]
    WHICH -->|ввод| INPUT["std::getline(cin),\nскопировать в Арену,\nreturn строка"]
    WHICH -->|выход| EXIT["std::exit(args[0].as.i64)"]
    WHICH -->|паника| PANICB["panic(args[0].as.s)"]
    WHICH -->|утверждение| ASSERT{"args[0].as.b?"}
    ASSERT -->|ложь| PANICA["panic('Утверждение ложно')"]
    ASSERT -->|истина| RETVOID["return Value()"]

    ISBUILTIN -->|нет| LOOKUP{"functions.count(имя)?"}
    LOOKUP -->|нет| PANICNF["panic('Функция не найдена\nв рантайме')"]
    LOOKUP -->|да| PROTOCHECK{"это только прототип\n(тело — InvalidNode)?"}
    PROTOCHECK -->|да| PANICPROTO["panic('Вызов функции\nбез определения')"]
    PROTOCHECK -->|нет| EVALARGS["вычислить все аргументы\n(eval для каждого,\nв области ВЫЗЫВАЮЩЕГО кода)"]

    EVALARGS --> SAVESCOPE["old_scopes = env.get_scopes()"]
    SAVESCOPE --> NEWSCOPE["new_scopes = [old_scopes[0], {}]\nenv.set_scopes(new_scopes)"]
    NEWSCOPE --> BINDARGS["для каждого параметра:\nenv.define(имя_параметра, args[i])"]
    BINDARGS --> DEPTHCHECK{"call_stack.size() > 1000?"}
    DEPTHCHECK -->|да| PANICSO["panic('Превышен лимит\nглубины рекурсии')"]
    DEPTHCHECK -->|нет| PUSHFRAME["call_stack.push_back(\n{имя, строка_вызова})"]
    PUSHFRAME --> EXECBODY["execute(тело функции)\n(Block — открывает свою\nобласть [2])"]
    EXECBODY --> POPFRAME["call_stack.pop_back()"]
    POPFRAME --> COLLECT["ret_val = return_value\nsould_return = false\nreturn_value = Value()"]
    COLLECT --> PROPAGATE["old_scopes[0] =\nenv.get_scopes()[0]\n(перенос глобальных изменений,\nсм. §5.1)"]
    PROPAGATE --> RESTORE["env.set_scopes(old_scopes)"]
    RESTORE --> RETURNVAL(["return ret_val"])

    style ISBUILTIN fill:#fef3c7,stroke:#b45309
    style PANICNF fill:#fee2e2,stroke:#b91c1c
    style PANICPROTO fill:#fee2e2,stroke:#b91c1c
    style PANICSO fill:#fee2e2,stroke:#b91c1c
```

Заметные детали, которые легко упустить при чтении кода линейно:

* **Аргументы вычисляются до подмены области видимости** (`EVALARGS` до `SAVESCOPE`) — поэтому выражения-аргументы видят переменные *вызывающего* кода, как и положено, а не пустой контекст вызываемой функции.
* Параметры функции всегда объявляются в `new_scopes[1]` — второй (после глобальной) области, поэтому тело функции их видит, но не видит `new_scopes[1]` вызывающего кода (замыканий нет, см. §5).
* Если тело функции — `Block`, оно откроет **третью** область (`env.enter_scope()` внутри `execute(Block)`) поверх параметров — это обычный вложенный блок, работает как везде.
* Проверка глубины рекурсии (`call_stack.size() > 1000`) происходит **после** подмены области видимости, но **до** фактического исполнения тела — то есть регресс по стеку вызовов регистрируется прежде, чем может произойти переполнение стека самого C++ (`eval`/`execute` рекурсивны на уровне хоста).

---

## 7. Массивы и структуры: единое представление, границы, вложенность

Массивы и структуры в рантайме — это **одно и то же представление**: `Value` с `kind ∈ {Array, Struct}`, указателем `as.arr` на блок в Арене и `count`. Разница между массивом и структурой существует только на уровне статических типов (`Semantic::Type`), не в рантайме — интерпретатор не различает их при индексации/сравнении, только `MemberAccess` (для структур) использует заранее вычисленный `offset`, а `Indexing` (для массивов) — вычисляемый в рантайме индекс.

```mermaid
flowchart LR
    subgraph STACK["Переменная в Environment"]
        VAR["Value{kind=Array, count=2,\nas.arr → ⬇}"]
    end
    subgraph ARENA["Арена"]
        E0["Value{kind=Struct, count=2,\nas.arr → ⬇}"]
        E1["Value{kind=Struct, count=2,\nas.arr → ⬇}"]
        F00["Value{kind=Int, i64=1}"]
        F01["Value{kind=Int, i64=2}"]
        F10["Value{kind=Int, i64=3}"]
        F11["Value{kind=Int, i64=4}"]
    end
    VAR --> E0
    VAR --> E1
    E0 --> F00
    E0 --> F01
    E1 --> F10
    E1 --> F11
```

Пример выше — `Точка[2] arr = [Точка{1,2}, Точка{3,4}];`: внешний `Value` (массив, `count=2`) указывает на два соседних `Value`-слота, каждый из которых сам — структура (`count=2`), указывающая на свои поля. Рекурсивность представления — причина, по которой ни `values_equal`, ни индексация, ни передача параметров не требуют отдельного кода для "массива структур" или "структуры с полем-массивом": каждый уровень обрабатывается одинаково, потому что каждый элемент — снова полноценный `Value`.

### 7.1 Проверка границ индекса

```mermaid
flowchart TD
    IDX(["eval(Indexing) или\nAssignStmt с целью Indexing"]) --> EVALARR["arr_val = eval(база)"]
    EVALARR --> EVALIDX["idx_val = eval(индекс)"]
    EVALIDX --> CHECK["check_index_bounds(arr_val, idx_val)"]
    CHECK --> COND{"idx < 0\nили\nidx >= arr_val.count?"}
    COND -->|да| PANIC["panic('Индекс N выходит\nза границы массива (размер M)')"]
    COND -->|нет| ACCESS["arr_val.as.arr[idx]\n(чтение или запись)"]
```

### 7.2 Глубокое сравнение `==`/`!=`

`values_equal(a, b)` — рекурсивная функция, ветвящаяся по `a.kind` (статический анализ уже гарантировал `a.kind == b.kind`):

```mermaid
flowchart TD
    EQ(["values_equal(a, b)"]) --> KIND{"a.kind"}
    KIND -->|"Int, Char"| CMP_I["a.as.i64 == b.as.i64"]
    KIND -->|Float| CMP_F["a.as.f64 == b.as.f64"]
    KIND -->|Bool| CMP_B["a.as.b == b.as.b"]
    KIND -->|String| CMP_S["strcmp(a.as.s, b.as.s) == 0\n(по содержимому, не по адресу)"]
    KIND -->|"Array, Struct"| CMP_C{"a.count == b.count?"}
    CMP_C -->|нет| FALSE(["false"])
    CMP_C -->|да| LOOP["для i in 0..count:\nvalues_equal(a.arr[i], b.arr[i])"]
    LOOP -->|"любой elem не равен"| FALSE
    LOOP -->|"все элементы равны"| TRUE(["true"])
```

Рекурсия в ветке `Array/Struct` — та же самая функция `values_equal`, поэтому сравнение массива структур или структуры с полем-массивом работает "само собой", без специального случая: на каждом уровне вложенности снова происходит диспетчеризация по `kind` конкретного элемента.

---

## 8. Обработка ошибок: `panic`

```mermaid
sequenceDiagram
    participant Node as Любой узел eval/execute
    participant Panic as VM::panic(msg)
    participant Stderr as stderr
    participant OS as std::exit(1)

    Node->>Panic: panic("сообщение")
    Panic->>Stderr: "Ошибка времени выполнения\n(строка N): сообщение"
    alt call_stack не пуст
        Panic->>Stderr: "Стек вызовов:"
        loop по call_stack (снизу вверх)
            Panic->>Stderr: "  в функции ИМЯ (строка K)"
        end
    end
    Panic->>OS: exit(1)
    Note over OS: процесс завершается немедленно —\nникакого раскручивания стека C++,\nникаких деструкторов Arena/Environment
```

`update_line(id)` вызывается в начале и `eval`, и `execute` для каждого узла — она обновляет `current_line` и (если есть активный вызов) `call_stack.back().call_line`, поэтому в момент паники и заголовок сообщения, и каждый кадр стека вызовов показывают корректную строку, на которой реально остановилось выполнение именно в этом кадре.

---

## 9. Сводная карта взаимодействия компонентов

```mermaid
flowchart TB
    subgraph Static["Статическая фаза (уже завершилась)"]
        AST["Плоское AST\n(nodes + child_indices)"]
    end

    subgraph Runtime["VM (рантайм)"]
        direction TB
        RUN["run(root)"]
        EXEC["execute(id)\nинструкции, эффект"]
        EVAL["eval(id)\nвыражения, Value"]
        ENV["Environment\nстек областей видимости"]
        FUNCS["functions:\nIdentId → NodeId"]
        FLAGS["should_return /\nshould_break /\nshould_continue"]
        CALLSTACK["call_stack\n(для паники и лимита рекурсии)"]
    end

    subgraph Mem["Memory::Arena"]
        POOL["StringPool\n(интернированные строки)"]
        BLOCKS["Блоки Value\n(массивы, структуры,\nстроки из ввод())"]
    end

    AST --> RUN
    RUN --> EXEC
    EXEC <--> EVAL
    EXEC --> ENV
    EVAL --> ENV
    EXEC --> FUNCS
    EVAL --> FUNCS
    EXEC --> FLAGS
    EVAL -->|"eval(Call) читает/сбрасывает\nshould_return"| FLAGS
    EVAL --> CALLSTACK
    EVAL --> BLOCKS
    EVAL --> POOL
    EVAL -.->|"панике при ошибке"| PANIC["VM::panic\n(stderr + exit(1))"]
    EXEC -.-> PANIC
    CALLSTACK -.->|"печатается\nпри панике"| PANIC

    style Static fill:#f3f4f6,stroke:#6b7280
    style Runtime fill:#dbeafe,stroke:#1d4ed8
    style Mem fill:#fef3c7,stroke:#b45309
```

**Итог одним предложением:** `VM` — это интерпретатор с обходом дерева (tree-walking), без байткода и без промежуточного IR; каждый узел AST выполняется/вычисляется напрямую по своему `NodeType`, состояние программы целиком живёт в трёх местах — `Environment` (переменные), Арена (данные массивов/структур/строк) и три флага управления потоком (`should_*`), — а корректность (типы, арность, наличие точки входа) полностью делегирована предыдущей фазе (`Semantic::Analyzer`) и в рантайме не перепроверяется, кроме того, что физически не может быть проверено статически: границы индекса массива, деление на ноль и глубина рекурсии.
