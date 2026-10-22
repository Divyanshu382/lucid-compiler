# Lucid

**Lucid** is a compiler for a small, statically-typed language, written in modern
C++17. It runs as a local HTTP server behind a browser *playground* that shows
every compiler phase live — tokens, a **graphical AST**, the symbol table, the
optimizer's before/after, and program output. When your code has an error, the
C++ compiler catches it deterministically and (optionally) a **Gemini Flash**
agent in the browser explains it in plain English and can auto-fix it.

> The C++ compiler is the star. The AI is an assistant layer: it never parses or
> generates program logic — it only *explains* errors and *proposes* fixes that
> are re-validated by re-running the real compiler.

---

## Architecture

```
Browser playground (HTML/CSS/JS)
   |  POST /compile  { "source": "..." }
   v
C++ HTTP server (cpp-httplib, single header)
   |
   v
C++ compiler:  Lexer -> Parser -> SemanticAnalyzer -> Optimizer -> IRGenerator -> Interpreter
   |
   returns JSON: { tokens, ast, symbolTable, optimized, ir, timings, output, errors }
```

- The **compiler** is pure C++17 — no framework, no network. Each phase lives in
  its own translation unit and is independently testable.
- The **server** (`lucid-server`) is a thin wrapper over the compiler using
  `cpp-httplib`. It also serves the static `web/` files, so there are no CORS
  issues.
- **Gemini** is called *from the browser* using a key you type into the UI. The
  whole app works with no key — you just lose the AI features.

Two executables share the same compiler core:

| Executable     | What it is                                                        |
|----------------|-------------------------------------------------------------------|
| `lucidc`       | Command-line compiler. No browser, no network. Prints every phase, the three-address code, and per-phase timings. |
| `lucid-server` | Local playground server (serves `web/`, handles `POST /compile`). |

---

## The language: a subset of C++

Lucid compiles a subset of C++, so ordinary first-course C++ programs run
unchanged:

```cpp
#include <iostream>
#include <string>
using namespace std;

const int LIMIT = 3;
int calls = 0;                       // globals are visible in functions

int square(int n) {
    calls++;
    return n * n;
}

int main() {
    string name;
    int n;
    cin >> name >> n;                // input comes from the STDIN box / stdin
    cout << "Hello, " << name << "!" << endl;
    for (int i = 1; i <= n; i++)
        if (i % 2) cout << i << " squared is " << square(i) << '\n';
        else       cout << i << " is even" << endl;
    cout << "calls: " << calls << endl;
    return 0;
}
```

**Supported:** `int`, `bool`, `string` (with `std::`), `const`; declarations
with or without a value and several per line; `cout << … << endl`, `'\n'`,
`cin >> …`; `if` / `else if` / `else`, `while`, `do … while`, `for`, `break`,
`continue`, `return`, with or without braces around single statements;
`+ - * / %`, `<< >>` shifts, comparisons (also on two strings), `&& || !`,
`?:`, `= += -= *= /= %=`, `x++ ++x x-- --x` as statements; casts `int(x)`,
`(int)x`, `static_cast<int>(x)`; `to_string(n)`; functions (including `void`,
recursion and prototypes); `#include` and `using namespace std;` are accepted
and ignored. A parameterless `main()` runs automatically.

**C++ rules followed:** ints and bools convert to each other (`if (n)`,
`while (n)`, `!n`, `true + 1`); integer `/` and `%` truncate toward zero;
`&&` and `||` short-circuit; a function sees the globals declared above it;
a bool printed with `cout` shows `1` or `0`. Results were checked against
clang++ on thousands of generated expressions and on whole programs.

**Where Lucid is stricter than C++, on purpose:** `"text" + n` is an error
(in C++ it silently moves a pointer, and clang warns about it), integer
overflow and division by zero are reported as runtime errors (undefined
behaviour in C++), and a string cannot be used as a condition.

**Not supported:** `double`, `char` variables, arrays and `vector`,
pointers and references, `struct` / `class`, templates, `++` inside a larger
expression, `switch`. Lucid's original `print(a, b)` and `func f(): int`
syntax still work.

Most of the convenience syntax is **sugar** handled in the parser (`for`
becomes a `while` with an update step, `x += e` and `x++` become
`x = x + …`, `int a, b;` becomes two declarations). C++'s implicit
int/bool conversions are made **explicit by the semantic analyser**, which
inserts `(int)` / `(bool)` cast nodes into the tree, so later phases never
see mixed types; the casts are visible in the syntax tree and the IR.

---

## Build

Requirements: a C++17 compiler and CMake ≥ 3.16. The two dependencies
(`nlohmann/json` and `cpp-httplib`) are vendored in `third_party/` — nothing to
install.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces `build/lucidc` and `build/lucid-server`.

---

## Run

### Playground (server)

```bash
./build/lucid-server          # defaults to port 8080
# or: ./build/lucid-server 9000
```

Then open **http://localhost:8080** in a browser.

> If something else already holds port 8080 (Docker commonly does), pass a
> different port: `./build/lucid-server 8099`, then open http://localhost:8099. Edit code on the left; the
tokens, AST, symbol table, output, optimizer, and errors update live as you type
(debounced ~300 ms). Use the dropdown to load the example gallery — one valid
program plus one intentionally-broken program per error category.

> Open it via `http://localhost:8080`, **not** by double-clicking
> `web/index.html` — the page needs the server's `/compile` endpoint.

### Windows (demo machine) — no compiler needed

The repository includes prebuilt Windows executables in `windows/`.

1. Clone or pull the repository.
2. Double-click **`run-windows.bat`**.
3. The first time, paste your Gemini API key when asked (or press Enter to
   skip AI). It is saved to `gemini.key`, which is git-ignored.
4. The browser opens **http://localhost:8099**. If Windows Firewall asks,
   click **Allow**.

Requires Windows 10 (1803) or later, which includes `curl.exe` for the AI
relay. If SmartScreen warns about an unrecognised app, choose
**More info → Run anyway** (the executables are unsigned).

The command-line compiler works too:
`windows\lucidc.exe examples\valid.lucid`

To rebuild the Windows executables after changing the C++ code, run
`./build-windows.sh` on macOS/Linux (needs `mingw-w64`). On Windows itself,
the normal CMake build also works with Visual Studio or MinGW.

### AI for every visitor (server key)

Put a Gemini key in **`gemini.key`** in the project root (or set the
`GEMINI_API_KEY` environment variable) and restart the server. Every visitor
then gets the AI features without typing a key: the browser sends requests to
the server's `/ai` relay, which adds the key, so the key never reaches a browser.
`gemini.key` is git-ignored. Anyone who can open the playground can use the
key's quota, so do not expose the server to the internet.

### Command line

```bash
./build/lucidc examples/valid.lucid
./build/lucidc program.cpp input.txt     # input.txt is what cin reads
echo "Ritu 5" | ./build/lucidc program.cpp
```

Prints tokens, a readable AST tree, the symbol table, optimizer before/after
counts, the three-address intermediate code, the program output, any errors
(with line + snippet), and the time each phase took in microseconds. Exit code is
`0` on success, `1` if the program had errors. This path uses **no browser and
no network**.

Sample programs live in `examples/` (a valid one plus one per error type).

---

## Intermediate code (three-address code)

After optimisation, Lucid lowers the tree into **quadruples** of the form
`(op, arg1, arg2, result)` — at most one operator per instruction.
Expressions are flattened into temporaries `t0, t1, …`, and control flow
becomes labels and jumps:

```
int total = 0;                       0      total = 0
for (int i = 1; i <= 3; i += 1) {    1      i = 1
    total += i;                      2  L0:
}                                    3      t0 = i <= 3
                                     4      iffalse t0 goto L1
                                     5      t1 = total + i
                                     6      total = t1
                                     7      t2 = i + 1
                                     8      i = t2
                                     9      goto L0
                                    10  L1:
```

Function calls use `param` / `call`, and function bodies use `recv` / `return`.
Arguments are all evaluated before any `param` is emitted, so nested calls such
as `add(square(3), 4)` come out in the correct order. The intermediate code is
generated for inspection and measured (quadruples, temporaries, labels);
execution is still performed by the tree-walking interpreter.

---

## Testing

```bash
python3 tests/run_tests.py        # add -v to list passing tests
```

42 test programs live in `tests/cases/`, grouped as **valid** (15),
**boundary** (9) and **errors** (18). Each file states its own expectation in
its first three lines, so a test is readable on its own:

```
// expect-exit: 1
// expect-output:
// expect-errors: semantic:TypeMismatch | semantic:TypeMismatch | semantic:TypeMismatch
```

Boundary cases include the empty program, comments only, `INT_MAX`, an
overflowing literal, deeply nested parentheses and `if`s, a zero-iteration
loop, and a 1,000-iteration loop. Error cases cover every diagnostic the
compiler can raise, including runtime division and modulo by zero.

---

## Performance

Every phase is timed with a monotonic clock inside the compiler. The CLI prints
the figures in microseconds; the playground's **Performance** tab draws them as
bars. The end-to-end figure also includes serialising each phase to JSON for
the playground, so it is larger than the sum of the phases.

---

## Robustness and limits

Lucid is tested against hostile input as well as normal programs:

- **Same results as C++.** 5,000 randomly generated expressions and a set of
  whole programs are compiled by both Lucid and clang++; the output is
  identical (precedence, associativity, `/` and `%` with negative numbers,
  short-circuit `&&`/`||`, `?:`, scoping, recursion).
- **Fuzzing.** Thousands of randomly corrupted programs, including under
  AddressSanitizer and UndefinedBehaviorSanitizer, produce only ordinary
  diagnostics: no crash, no hang.
- **Integer overflow is a runtime error.** In C++ it is undefined behaviour,
  and `INT_MIN / -1` even crashes the program; Lucid reports
  `Integer overflow: ... does not fit in an int` (like C++'s
  `-fsanitize=undefined`). `INT_MIN % -1` is 0.

Limits, each reported as an ordinary error:

| Limit | Value |
|---|---|
| Nesting of brackets, blocks and unary operators | 256 levels (clang's default bracket limit is also 256) |
| Depth of the syntax tree (e.g. very long operator chains) | 2,000 levels |
| Nested function calls (recursion) | 10,000 |
| Steps (loop iterations + calls) per run | 2,000,000 |
| Length of one string | 1,000,000 characters |
| Program output | 10,000 lines / 2 MB |

Each compilation runs on its own thread with a 256 MB stack
([`src/bigstack.cpp`](src/bigstack.cpp)), so deep recursion cannot overflow
the small stacks of server worker threads (512 KB on macOS) or the Windows
main thread (1 MB).

---

## Gemini API key (AI features)

The AI features are **optional**. Get a free key at
[Google AI Studio](https://aistudio.google.com/apikey), paste it into the
**Assistant** field under the editor (it is kept in your browser's
`localStorage` only — never in this repository), and five features switch on:

| Feature | Where | What it does |
|---|---|---|
| **Explain** | on each problem | Beginner explanation of the compiler's diagnostic, plus one concrete fix |
| **Fix with AI** | on each problem | The auto-fix agent loop (below) |
| **Explain program** | Assistant bar, under the editor | Plain-English walkthrough of what the program does |
| **Write from description** | Assistant bar, under the editor | Describe a program in English; Gemini writes it in Lucid and the compiler checks it. A rejected program goes straight to the auto-fix loop |
| **Explain this code with AI** | IR phase | Explains how the source became three-address code — what each temporary holds and how the jumps implement `if`/`while`/`for` |

**The authority rule.** The AI is never part of the compiler: it never parses,
type-checks, or decides whether a program is correct. It may explain, and it
may *propose* source code — but every proposal is recompiled by the C++
compiler, and only the compiler's verdict counts.

With no key, nothing touches the network: each AI action shows the *exact*
request that would be sent, so the pipeline is still demonstrable offline.

**Models and reliability.** Requests try `gemini-3.5-flash`, then
`gemini-3.6-flash`, then `gemini-3.8-flash` (list at the top of
[`web/app.js`](web/app.js)). A busy or unavailable model falls through to the
next; a free-tier rate limit (HTTP 429) with a short suggested wait is retried
once after that wait; each request times out after 30 s. Every reply says which
model answered and how long it took.

### The auto-fix loop (the "agent")

**Fix with AI** runs a real *perceive → act → observe* cycle: send the broken code
+ the compiler's error to Gemini, get corrected source, **re-POST it to
`/compile`**, and repeat up to 3 times until it compiles or the limit is hit.
Every attempt is shown. The compiler — not the model — decides success.

---

## Security note

The API key never needs to be in the browser or in this repository. With a
**server key** (`gemini.key` or `GEMINI_API_KEY`), the C++ server relays AI
requests through `POST /ai` and adds the key itself; the key is passed to
`curl` through its stdin, never on the command line. The relay only accepts
requests from the playground's own page (Origin check), only plain
`gemini-…` model ids, and prompts up to 64 KB. A visitor may still paste their
own key, which is then used directly from their browser instead.

Anyone who can reach the server can spend the server key's quota, so keep the
playground on your own machine or local network.

---

## Project layout

```
CMakeLists.txt
third_party/
  httplib.h            # cpp-httplib (single header)
  json.hpp             # nlohmann/json (single header)
src/
  token.hpp            # Token struct + TokenType enum
  error.hpp            # Error struct + ErrorList helpers
  ast.hpp              # AST node classes, each with toJson()
  lexer.hpp/.cpp       # Phase 1: text  -> tokens
  parser.hpp/.cpp      # Phase 2: tokens -> AST (recursive descent)
  semantic.hpp/.cpp    # Phase 3: type checking + symbol table + "did you mean?"
  optimizer.hpp/.cpp   # Phase 4: constant folding + dead-code elimination
  ircode.hpp/.cpp      # Phase 5: three-address code (quadruples)
  ai_proxy.hpp/.cpp    # server only: Gemini relay for the server key (not compiler code)
  bigstack.hpp/.cpp    # runs each compilation on a 256 MB stack
  intmath.hpp          # overflow-checked integer arithmetic
  interpreter.hpp/.cpp # Phase 6: tree-walking evaluator
  compiler.hpp/.cpp    # orchestrates the phases, builds the result JSON
  main_cli.cpp         # `lucidc`  entry point
  main_server.cpp      # `lucid-server` entry point
web/
  index.html style.css app.js examples.js
examples/
  valid.lucid + one program per error type
tests/
  run_tests.py         # test runner
  cases/{valid,boundary,errors}/*.lucid
README.md
```

Every compiler file starts with a comment naming the phase it implements and its
input/output, so each phase is easy to explain out loud.

---

## Error taxonomy

| Phase     | Errors caught |
|-----------|---------------|
| Lexical   | unknown character; unterminated string |
| Syntax    | unexpected token; missing `;`, `)`, `}`; malformed statement; integer overflow |
| Semantic  | undeclared / redeclared variable; type mismatch in declaration and operations; non-boolean condition; call to undefined function; wrong argument count / types; return-type mismatch |
| Runtime   | division / modulo by zero; integer overflow; runaway recursion (call depth > 10,000); execution budget exceeded (> 2,000,000 loop iterations + calls — catches infinite loops); output over 10,000 lines |

Semantic errors are reported **all in one pass** (the checker doesn't stop at the
first). Undeclared names get a Levenshtein-based *"Did you mean …?"* suggestion.

---

## Terminal-only variant (Path B)

If you don't want the web playground, the CLI (`lucidc`) already is a complete
interface: tokens, AST, symbol table, optimizer before/after, errors, and output
all print to the terminal with no browser and no network. To move the AI into
C++ as well, add an `--explain` flag that POSTs the same JSON body to the same
Gemini endpoint with libcurl and parses the reply with `nlohmann/json`; the
auto-fix loop becomes: get corrected source, re-run `lucid::compile` in-process,
repeat up to 3 times.
