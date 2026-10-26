# Lambda Interpreter

English | [中文](README.zh-CN.md)

A tree-walking interpreter, written in C, for a small expression-oriented language with first-class functions, closures, conditionals, `let`/`def` bindings and assignment. The same program can be run under **lexical** or **dynamic** scope, which makes the difference between the two easy to see.

```
$ cat examples/scope.417
{
  let base = 10;
  let add_base = λ(n) { add(n, base) };
  let base = 100;
  add_base(1)
}

$ ./parse < examples/scope.417 | ./interpreter
11
$ ./parse < examples/scope.417 | ./interpreter --dynamic
101
```

## Features

- Integers (64-bit, overflow is reported as an error), booleans and strings
- First-class functions and closures, including functions that return functions and closures with private mutable state
- Lexical or dynamic scope, chosen with a command-line flag
- `let`, `def`, assignment, `cond`, and blocks
- Built-ins: `add`, `sub`, `mul`, `eq`, `zero?`, `print`
- Clear error messages for unbound names, wrong argument counts, type errors, and runaway recursion, instead of crashes

## How it works

```
program.417 ──► parse ──► JSON syntax tree ──► interpreter ──► result
```

Parsing is done by a separate program, the 417 parser (see [Credits](#credits)). It turns source code into a JSON syntax tree, which the interpreter reads from stdin with the [Jansson](https://github.com/akheron/jansson) library and evaluates.

- **Environments.** Each scope is a list of bindings with a link to its parent scope. Looking up a name walks outward until it finds a binding.
- **Closures.** Evaluating a `λ` creates a function value that keeps a pointer to the environment it was created in.
- **Lexical vs. dynamic scope.** The only difference is one line in `apply_function`: the new scope for a call extends either the function's saved environment (lexical) or the caller's environment (dynamic).
- **Memory.** A closure can outlive the call that created it (for example, a function that returns a function), so environments cannot be freed when a call returns. All runtime objects are recorded as they are allocated and released together when the program ends. AddressSanitizer reports no memory errors or leaks on the test suite.
- **Safety limits.** Integer arithmetic is checked for overflow, and recursion deeper than 5,000 nested calls stops with an error rather than overflowing the C stack.

## Getting started

You need a C compiler, `make`, `git`, and the Jansson library.

| Platform | Install |
| --- | --- |
| Ubuntu / Debian / WSL | `sudo apt install build-essential git libjansson-dev` |
| macOS (Homebrew) | `brew install jansson` |
| Windows (MSYS2 UCRT64) | `pacman -S make git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-jansson` |

Then build the interpreter, download and build the parser, and run the tests:

```bash
make            # builds ./interpreter
make parser     # downloads and builds ./parse
make test
```

On macOS, point the compiler at Homebrew: `make CPPFLAGS=-I$(brew --prefix)/include LDFLAGS=-L$(brew --prefix)/lib`.

Tested on Ubuntu 22.04. On Windows, WSL is the most straightforward option.

## Usage

```bash
./parse < examples/closures.417 | ./interpreter            # lexical scope (default)
./parse < examples/closures.417 | ./interpreter --dynamic  # dynamic scope
```

The interpreter prints anything the program prints with `print`, then the value of the whole program. On an error it prints a message starting with `Error:` and exits with status 1.

## The language

| Form | Example | Meaning |
| --- | --- | --- |
| Literal | `42`, `"hello"` | an integer or a string |
| Function | `λ(a, b) { add(a, b) }` | creates a function (`lambda` also works) |
| Call | `f(1, 2)` | calls a function |
| Block | `{ e1; e2; e3 }` | evaluates in order; the value is the last one |
| Let | `let x = 1; ...` | binds `x` for the rest of the block |
| Def | `def x = 1` | adds `x` to the current scope |
| Assignment | `x = 2` | changes an existing binding |
| Cond | `cond (test => a) (true => b)` | the value of the first clause whose test is true |

Built-in functions: `add`, `sub`, `mul` (integers), `eq` (two integers, booleans, or strings), `zero?`, and `print`. The initial environment also binds `true`, `false`, `x = 10`, `v = 5`, and `i = 1`.

## Examples

| File | What it shows | Lexical | Dynamic |
| --- | --- | --- | --- |
| `scope.417` | the same program under both scoping rules | `11` | `101` |
| `closures.417` | a function that returns a function | `15` | error |
| `counter.417` | a closure with private, mutable state | `3` | error |
| `factorial.417` | recursion through assignment | `3628800` | `3628800` |
| `self_application.417` | recursion by passing a function to itself | `3628800` | `3628800` |
| `fibonacci.417` | recursion through `def` | `6765` | `6765` |
| `strings.417` | strings, `print`, and `eq` | `hello` `417` `true` | same |

## Tests

`make test` runs `tests/run_tests.sh`: every example under both scoping modes, plus small programs for each built-in and each kind of error (33 checks).

## Project structure

```
interpreter.c        the interpreter (values, environments, built-ins, evaluator)
examples/            sample programs
tests/run_tests.sh   test runner
Makefile             build, fetch the parser, run the tests
```

## Credits

The 417 language and its parser were created by Jamie A. Jennings and are available at [gitlab.com/JamieTheRiveter/csc417](https://gitlab.com/JamieTheRiveter/csc417) under the MIT license. `make parser` downloads the parser; it is not included in this repository.
