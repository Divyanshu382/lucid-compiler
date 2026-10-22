// ============================================================================
// examples.js — example programs for the Lucid playground, written in the
// subset of C++ that Lucid compiles. Programs first, then one intentionally
// broken program per kind of error, so every diagnostic (and the AI
// assistant) can be demonstrated on demand. `stdin` is what cin will read.
// ============================================================================
window.LUCID_EXAMPLES = [
  {
    name: "Full feature tour", group: "Programs",
    code: `#include <iostream>
#include <string>
using namespace std;

int add(int a, int b) {
    return a + b;
}

int main() {
    int x = 5;
    string name = "Lucid";
    bool ready = true;

    x += 10;
    if (x > 10)
        cout << name << " says: big" << endl;
    else
        cout << name << " says: small" << endl;

    for (int i = 0; i < 3; i++)
        cout << "i = " << i << endl;

    cout << "3 + 4 = " << add(3, 4) << endl;
    cout << "ready: " << ready << endl;      // a bool prints as 1
    return 0;
}
`
  },
  {
    name: "Reading input with cin", group: "Programs", stdin: "Ritu 5",
    code: `#include <iostream>
#include <string>
using namespace std;

int main() {
    string name;
    int n;
    cin >> name >> n;                  // values come from the STDIN box

    cout << "Hello, " << name << "!" << endl;
    int sum = 0;
    for (int i = 1; i <= n; i++)
        sum += i;
    cout << "1 + 2 + ... + " << n << " = " << sum << endl;
    return 0;
}
`
  },
  {
    name: "Loops, else-if and +=", group: "Programs",
    code: `#include <iostream>
using namespace std;

int main() {
    int total = 0;
    for (int i = 1; i <= 10; i++)
        total += i;
    cout << "total = " << total << endl;

    int score = 72;
    if (score >= 90)
        cout << "grade A" << endl;
    else if (score >= 70)
        cout << "grade B" << endl;
    else
        cout << "grade C" << endl;

    int n = 5;
    while (n)                          // non-zero counts as true
        n--;
    cout << "n = " << n << endl;
    return 0;
}
`
  },
  {
    name: "break, continue, ?: and do-while", group: "Programs",
    code: `#include <iostream>
using namespace std;

int maxOf(int a, int b) {
    return a > b ? a : b;              // conditional expression
}

void countdown(int from) {
    do {
        cout << "t-minus " << from << endl;
        from--;
    } while (from > 0);
}

int main() {
    int total = 0, skipped = 0;
    for (int i = 1; i <= 10; i++) {
        if (i % 3 == 0) { skipped++; continue; }
        if (i > 8) break;
        total += i;
    }
    cout << "total = " << total << ", skipped " << skipped << endl;
    cout << "max is " << maxOf(4, 9) << endl;
    countdown(3);
    return 0;
}
`
  },
  {
    name: "Recursion: factorial and Fibonacci", group: "Programs",
    code: `#include <iostream>
using namespace std;

int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}

int fib(int n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main() {
    cout << "5! = " << fact(5) << endl;
    cout << "fib(10) = " << fib(10) << endl;
    return 0;
}
`
  },
  {
    name: "Optimizer: folding and dead code", group: "Programs",
    code: `#include <iostream>
using namespace std;

int f() {
    return 10 + 5;                     // folds to 15
    cout << "dead code";               // removed: after return
}

int main() {
    // Watch the Optimizer phase: the tree shrinks.
    int a = 2 + 3 * 4;                 // folds to 14
    bool t = 1 < 2 && true;            // folds to true

    if (false)
        cout << "unreachable" << endl; // whole branch removed
    else
        cout << "kept" << endl;

    if (2 > 1)
        cout << "always runs" << endl; // if() unwrapped

    cout << a << " " << t << " " << f() << endl;
    return 0;
}
`
  },
  {
    name: "Type mismatch (declaration)", group: "Error cases",
    code: `#include <iostream>
#include <string>
using namespace std;

int main() {
    int x = "hello";
    string name = 42;
    string flag = true;
    return 0;
}
`
  },
  {
    name: "Type mismatch (operation)", group: "Error cases",
    code: `#include <string>
using namespace std;

int main() {
    string s = "a";
    int n = s - 1;
    string t = s * 2;
    return 0;
}
`
  },
  {
    name: "Text plus a number", group: "Error cases",
    code: `#include <iostream>
#include <string>
using namespace std;

int main() {
    int age = 21;
    string msg = "Age: " + age;        // C++ rejects this: use to_string(age)
    cout << msg << endl;
    return 0;
}
`
  },
  {
    name: "Undeclared variable (did you mean?)", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int main() {
    int count = 5;
    count = count + 1;
    cout << cont << endl;
    return 0;
}
`
  },
  {
    name: "Redeclaration in same scope", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int main() {
    int score = 10;
    int score = 20;
    cout << score << endl;
    return 0;
}
`
  },
  {
    name: "Condition that is not true/false", group: "Error cases",
    code: `#include <iostream>
#include <string>
using namespace std;

int main() {
    string name = "Ritu";
    if (name)                          // a string is not a condition
        cout << "hello" << endl;
    return 0;
}
`
  },
  {
    name: "Assigning to a const", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int main() {
    const int MAX = 10;
    MAX = 20;
    cout << MAX << endl;
    return 0;
}
`
  },
  {
    name: "Division by zero (runtime)", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int main() {
    int a = 10;
    int b = 0;
    cout << a / b << endl;
    return 0;
}
`
  },
  {
    name: "Input runs out (runtime)", group: "Error cases", stdin: "5",
    code: `#include <iostream>
using namespace std;

int main() {
    int a, b;
    cin >> a >> b;                     // STDIN has only one value
    cout << a + b << endl;
    return 0;
}
`
  },
  {
    name: "Missing semicolon", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int main() {
    int x = 5
    cout << x << endl;
    return 0;
}
`
  },
  {
    name: "Missing brace / paren", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int main() {
    int x = 5;
    if (x > 1 {
        cout << "oops" << endl;
    }
    return 0;
}
`
  },
  {
    name: "Call to undefined function", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int square(int n) {
    return n * n;
}

int main() {
    cout << squar(4) << endl;
    return 0;
}
`
  },
  {
    name: "Wrong number of arguments", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int add(int a, int b) {
    return a + b;
}

int main() {
    cout << add(1) << endl;
    return 0;
}
`
  },
  {
    name: "Wrong argument type", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int add(int a, int b) {
    return a + b;
}

int main() {
    cout << add(1, "two") << endl;
    return 0;
}
`
  },
  {
    name: "Return type mismatch", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int greet() {
    return "hi";
}

int main() {
    cout << greet() << endl;
    return 0;
}
`
  },
  {
    name: "Unterminated string", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int main() {
    cout << "no closing quote;
    return 0;
}
`
  },
  {
    name: "Unknown character", group: "Error cases",
    code: `#include <iostream>
using namespace std;

int main() {
    int x = 5;
    int y = x @ 2;
    cout << y << endl;
    return 0;
}
`
  }
];
