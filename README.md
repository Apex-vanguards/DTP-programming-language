# DTP — DarkDevil Tuncypasha Programmercat

A fast, lean, zero-overhead systems programming language that compiles to C99 and then to native machine code via GCC or Clang.

```
source.dtp
    │
    ▼  Lexer         — characters → tokens
    │
    ▼  Parser        — tokens → AST
    │
    ▼  TypeChecker   — type inference & validation
    │
    ▼  CodeGen       — AST → C99 source
    │
    ▼  GCC/Clang -O3 -march=native
    │
    ▼  native binary
```

---

## Installation

```bash
git clone <repo>
cd dtp

# User install (no sudo) — installs to ~/.dtp/
bash install.sh

# System install (requires sudo) — installs to /usr/local/bin/
bash install.sh --system
```

`install.sh` will:

1. Build `dtpc` using the C++ compiler on your system
2. Copy the binary to `~/.dtp/dtpc` (or `/usr/local/bin/dtpc`)
3. Copy the standard library to `~/.dtp/stdlib/`
4. Add `~/.dtp` to your `PATH` in `.bashrc` / `.zshrc`

After installation, open a new terminal (or `source ~/.bashrc`) and run:

```bash
dtpc --help
```

---

## Quick Start

```bash
# Compile and run
dtpc hello.dtp -o hello
./hello

# Print the generated C (useful for debugging)
dtpc hello.dtp --emit-c

# Maximum performance
dtpc prog.dtp -O3 --cc clang -o prog

# Type-check only, no output
dtpc prog.dtp --check
```

---

## Language Reference

### Variables

```dtp
let x: i64 = 42;         // immutable
let mut y: i64 = 0;      // mutable
let pi: f64 = 3.14159;
let active: bool = true;
let ch: char = 'A';
let msg: str = "hello";
```

`let` is immutable by default. Use `mut` to allow reassignment.

---

### Types

| Type   | Size | Range / Description           |
|--------|------|-------------------------------|
| `i8`   | 1B   | -128 .. 127                   |
| `i16`  | 2B   | -32768 .. 32767               |
| `i32`  | 4B   | -2^31 .. 2^31-1               |
| `i64`  | 8B   | -2^63 .. 2^63-1               |
| `u8`   | 1B   | 0 .. 255                      |
| `u16`  | 2B   | 0 .. 65535                    |
| `u32`  | 4B   | 0 .. 2^32-1                   |
| `u64`  | 8B   | 0 .. 2^64-1                   |
| `f32`  | 4B   | 32-bit float                  |
| `f64`  | 8B   | 64-bit float                  |
| `bool` | 1B   | true / false                  |
| `char` | 1B   | ASCII character               |
| `str`  | 8B   | string pointer (const char*)  |
| `*T`   | 8B   | pointer to T                  |
| `[T]`  | 16B  | slice: ptr + length           |

---

### Input

```dtp
let n: i64 = read_i64();       // read integer from stdin
let x: f64 = read_f64();       // read float
let s: str = read_str();       // read a line of text (newline stripped)
let b: bool = read_bool();     // reads "true"/"1"/"y" → true
```

---

### Functions

```dtp
fn add(a: i64, b: i64) -> i64 {
    ret a + b;
}

inline fn square(x: f64) -> f64 {
    ret x * x;
}

fn greet(name: str) {
    println(name);
}

ext fn printf(fmt: str) -> i32;

comptime fn power_of_two(n: i64) -> i64 {
    ret 1 << n;
}
```

---

### Control Flow

```dtp
if x > 0 {
    println("positive");
} elif x == 0 {
    println("zero");
} else {
    println("negative");
}

let mut i: i64 = 0;
while i < 10 {
    println(i);
    i = i + 1;
}

loop {
    if done { break; }
}

for i in 0..10  { println(i); }  // 0 to 9
for i in 1...5  { println(i); }  // 1 to 5 inclusive
```

---

### Structs

```dtp
struct Point {
    pub x: f64,
    pub y: f64,
}

fn main() -> i32 {
    let p: Point = Point { x: 3.0, y: 4.0 };
    println(p.x);
    ret 0;
}
```

#### Methods with `impl`

```dtp
struct Counter {
    pub value: i64,
    pub step:  i64,
}

impl Counter {
    fn new(step: i64) -> Counter {
        ret Counter { value: 0, step: step };
    }
    fn advance(c: Counter) -> Counter {
        ret Counter { value: c.value + c.step, step: c.step };
    }
}

fn main() -> i32 {
    let mut c: Counter = Counter_new(5);
    c = Counter_advance(c);
    println(c.value);
    ret 0;
}
```

---

### Enums

```dtp
enum Direction {
    North = 0,
    South,
    East,
    West,
}

fn main() -> i32 {
    let d: Direction = Direction_North;
    if d == Direction_North {
        println("heading north");
    }
    ret 0;
}
```

---

### Memory Management

No garbage collector. Memory is yours to manage.

```dtp
let p: *Point = new Point;
// use p
drop(p);

fn process() {
    let buf: *u8 = new u8;
    defer drop(buf);
    // buf is freed at end of function no matter what
}

let x: i64 = 42;
let ptr: *i64 = &x;
let val: i64 = *ptr;
```

---

### Operators

```
Arithmetic:   +  -  *  /  %
Bitwise:      &  |  ^  ~  <<  >>
Comparison:   ==  !=  <  >  <=  >=
Logical:      and  or  not
Assignment:   =  +=  -=  *=  /=  &=  |=  ^=
Other:        &x (address-of)   *p (dereference)
              x as T (cast)     sizeof(T)
```

---

### Built-in Functions

| Function        | Description                         |
|-----------------|-------------------------------------|
| `print(x)`      | Print value (no newline)            |
| `println(x)`    | Print value + newline               |
| `eprint(x)`     | Print to stderr                     |
| `read_i64()`    | Read integer from stdin             |
| `read_f64()`    | Read float from stdin               |
| `read_str()`    | Read line from stdin                |
| `read_bool()`   | Read boolean from stdin             |
| `sizeof(T)`     | Byte size of type T                 |
| `len(slice)`    | Length of slice                     |
| `new T`         | Allocate T on heap                  |
| `drop(ptr)`     | Free heap memory                    |

---

## Module System

### Using other DTP files

```dtp
use math.dtp;
use io.dtp;
```

`use foo.dtp` is resolved in this order:

1. Same directory as the current source file
2. `~/.dtp/stdlib/`
3. `/usr/local/share/dtp/stdlib/`
4. Directories in `DTP_PATH` environment variable (colon-separated)

### Compiling multiple files

```bash
# math.dtp is passed on the command line
dtpc main.dtp math.dtp -o main

# Or let dtpc resolve it automatically if main.dtp has `use math.dtp;`
dtpc main.dtp -o main
```

All files are merged into one combined module before type-checking and code generation.

---

## Standard Library

The stdlib lives in `stdlib/` and is installed to `~/.dtp/stdlib/` by `install.sh`.

| File       | Contents                                          |
|------------|---------------------------------------------------|
| `math.dtp` | sqrt, pow, sin, cos, log, abs, min, max, gcd, lcm, pow_i64, is_prime |
| `io.dtp`   | printf, scanf, fflush, getchar, putchar           |
| `mem.dtp`  | malloc, calloc, realloc, free, memcpy, memset     |
| `str.dtp`  | strlen, strcpy, strcmp, strstr, atoi, atof        |

Usage example:

```dtp
use math.dtp;

fn main() -> i32 {
    let r: f64 = sqrt(2.0);
    println(r);
    let p: i64 = pow_i64(2, 10);
    println(p);
    ret 0;
}
```

Compile:

```bash
dtpc main.dtp -o main
```

---

## Writing Your Own Libraries

Create `mylib.dtp`:

```dtp
pub fn fibonacci(n: i64) -> i64 {
    if n <= 1 { ret n; }
    let mut a: i64 = 0;
    let mut b: i64 = 1;
    let mut i: i64 = 2;
    while i <= n {
        let t: i64 = a + b;
        a = b;
        b = t;
        i = i + 1;
    }
    ret b;
}
```

Use it in `main.dtp`:

```dtp
use mylib.dtp;

fn main() -> i32 {
    println(fibonacci(30));
    ret 0;
}
```

Compile:

```bash
dtpc main.dtp mylib.dtp -o main
# or if mylib.dtp is in the same directory, just:
dtpc main.dtp -o main
```

---

## Calling C Libraries

```dtp
struct Vec3 {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

ext fn vec3_add(a: Vec3, b: Vec3) -> Vec3;
ext fn vec3_length(v: Vec3) -> f64;

fn main() -> i32 {
    let a: Vec3 = Vec3 { x: 1.0, y: 0.0, z: 0.0 };
    let b: Vec3 = Vec3 { x: 0.0, y: 1.0, z: 0.0 };
    let c: Vec3 = vec3_add(a, b);
    println(vec3_length(c));
    ret 0;
}
```

```bash
gcc -O3 -c libvec.c -o libvec.o
ar rcs libvec.a libvec.o
dtpc main.dtp -o main -L. -lvec -lm
```

---

## Compiler Options

```
dtpc <file.dtp> [extra.dtp ...] [options]

  -o <out>         Output binary name (default: input stem)
  --emit-c         Print generated C to stdout
  -c <file.c>      Write generated C to file
  --check          Type-check only, no output
  -O0/-O1/-O2/-O3  Optimization level (default: -O2)
  --cc <compiler>  C backend compiler (default: gcc)
  -l<lib>          Pass -l flag to linker
  -v               Verbose (show each pipeline stage)
  --no-color       Disable ANSI color output
  --help           Show this help
```

---

## Error Reference

| Error Message                     | Cause                           | Fix                           |
|-----------------------------------|---------------------------------|-------------------------------|
| `Undefined variable: 'x'`         | Variable not declared           | Add `let x: i64 = 0;`         |
| `Return type mismatch`            | Wrong return type               | Check `ret` value type        |
| `Cannot assign to immutable`      | Missing `mut`                   | Use `let mut x`               |
| `No field 'f' in struct 'S'`      | Wrong field name                | Check struct definition       |
| `Parse error ... expected ')'`    | Unmatched parenthesis           | Count your brackets           |
| `Cannot infer type of 'x'`        | No type annotation or init expr | Add `: i64` type annotation   |

---

## Project Structure

```
dtp/
├── install.sh            # One-shot installer
├── Makefile              # Build rules
├── README.md             # This file
├── include/
│   ├── dtp.hpp           # AST nodes, token types, symbol table
│   ├── lexer.hpp         # Source → tokens
│   ├── parser.hpp        # Tokens → AST
│   ├── typechecker.hpp   # Type inference & validation
│   └── codegen.hpp       # AST → C99
├── src/
│   └── dtpc.cpp          # Entry point, CLI, multi-file driver
├── stdlib/
│   ├── math.dtp          # Math functions
│   ├── io.dtp            # I/O helpers
│   ├── mem.dtp           # Memory (malloc/free/memcpy)
│   └── str.dtp           # String utilities
└── examples/
    ├── fib.dtp
    ├── sort.dtp
    └── structs.dtp
```

---

## How Performance Works

DTP generates clean, type-safe C99 which GCC/Clang can fully optimize:

- Auto-vectorization (AVX2 / SSE4.2)
- Loop unrolling
- Aggressive inlining
- Dead code elimination
- Native CPU instructions (`-march=native`)
- Branch prediction hints (`DTP_LIKELY` / `DTP_UNLIKELY` macros)

The result is performance on par with hand-tuned C.

---

## Planned Features

- [ ] Generic types: `fn max<T>(a: T, b: T) -> T`
- [ ] Pattern matching: `match x { ... }`
- [ ] Trait system
- [ ] Multi-file incremental compilation (cache per-module C output)
- [ ] Standard library: `std::collections`, `std::net`
- [ ] Error propagation: `?` operator
- [ ] LLVM backend (direct binary, no C step)
