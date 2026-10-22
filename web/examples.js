// ============================================================================
// examples.js — the broken-example gallery for the Lucid playground.
// One valid program plus one intentionally-broken program per error category,
// so every diagnostic (and the AI explainer) can be demoed on demand.
// ============================================================================
window.LUCID_EXAMPLES = [
  {
    name: "✅ Valid — full feature tour",
    code: `int x = 5;
string name = "Divyanshu";
bool ready = true;

x = x + 10;

if (x > 10) {
    print("big");
} else {
    print("small");
}

int i = 0;
while (i < 5) {
    print(i);
    i = i + 1;
}

func add(int a, int b): int {
    return a + b;
}

print(add(3, 4));
`
  },
  {
    name: "🔁 For loops, else-if, +=",
    code: `// for-loops, else-if chains and compound assignment
// are desugared by the parser into while / if / '=' nodes.
int total = 0;
for (int i = 1; i <= 10; i += 1) {
    total += i;
}
print(total);

int score = 72;
if (score >= 90) {
    print("grade A");
} else if (score >= 70) {
    print("grade B");
} else {
    print("grade C");
}
`
  },
  {
    name: "🧩 C-style syntax — main, break, ?:, do-while",
    code: `/* Lucid also understands familiar C-style syntax. */
int max(int a, int b) {
    return a > b ? a : b;          // conditional expression
}

void countdown(int from) {
    do {
        print("t-minus", from);
        from--;
    } while (from > 0);
}

int main() {                       // runs automatically
    int total = 0, skipped;
    for (int i = 1; i <= 10; i++) {
        if (i % 3 == 0) { skipped++; continue; }
        if (i > 8) { break; }
        total += i;
    }
    print("total = " + total, "skipped:", skipped);
    print("max is", max(4, 9));
    countdown(3);
    return 0;
}
`
  },
  {
    name: "🌀 Recursion — factorial & Fibonacci",
    code: `func fact(int n): int {
    if (n <= 1) { return 1; }
    return n * fact(n - 1);
}

func fib(int n): int {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}

print(fact(5));
print(fib(10));
`
  },
  {
    name: "⚡ Optimizer — folding + dead code",
    code: `// Watch the Optimizer tab: nodes shrink a lot.
int a = 2 + 3 * 4;         // folds to 14
bool t = 1 < 2 && true;    // folds to true

if (false) {
    print("unreachable");  // whole branch removed
} else {
    print("kept");
}

if (2 > 1) {
    print("always runs");  // if() unwrapped
}

func f(): int {
    return 10 + 5;         // folds to 15
    print("dead code");    // removed: after return
}

print(f());
`
  },
  {
    name: "❌ Type mismatch (declaration)",
    code: `int x = "hello";
string name = 42;
bool ok = 3 + 4;
`
  },
  {
    name: "❌ Type mismatch (operation)",
    code: `string s = "a";
int n = s - 1;
bool b = true + false;
`
  },
  {
    name: "❌ Undeclared variable (did you mean?)",
    code: `int count = 5;
count = count + 1;
print(cont);
`
  },
  {
    name: "❌ Redeclaration in same scope",
    code: `int score = 10;
int score = 20;
print(score);
`
  },
  {
    name: "❌ Condition not boolean",
    code: `int x = 5;
if (x) {
    print("this needs a bool");
}
while (x + 1) {
    print("also needs a bool");
}
`
  },
  {
    name: "❌ Division by zero (runtime)",
    code: `int a = 10;
int b = 0;
print(a / b);
`
  },
  {
    name: "❌ Missing semicolon",
    code: `int x = 5
print(x);
`
  },
  {
    name: "❌ Missing brace / paren",
    code: `int x = 5;
if (x > 1 {
    print("oops");
}
`
  },
  {
    name: "❌ Call to undefined function",
    code: `func square(int n): int {
    return n * n;
}
print(squar(4));
`
  },
  {
    name: "❌ Wrong number of arguments",
    code: `func add(int a, int b): int {
    return a + b;
}
print(add(1));
`
  },
  {
    name: "❌ Wrong argument type",
    code: `func add(int a, int b): int {
    return a + b;
}
print(add(1, "two"));
`
  },
  {
    name: "❌ Return type mismatch",
    code: `func greet(): int {
    return "hi";
}
print(greet());
`
  },
  {
    name: "❌ Unterminated string",
    code: `string s = "no closing quote;
print(s);
`
  },
  {
    name: "❌ Unknown character",
    code: `int x = 5;
int y = x @ 2;
print(y);
`
  }
];
