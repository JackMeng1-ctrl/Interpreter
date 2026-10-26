# Lambda 解释器

[English](README.md) | 中文

一个用 C 语言写的树遍历解释器，运行一门小型的、以表达式为中心的语言。语言支持一等函数、闭包、条件表达式、`let`/`def` 绑定和赋值。同一个程序可以分别在**词法作用域**和**动态作用域**下运行，方便直观地看出两者的区别。

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

## 功能

- 整数（64 位，溢出时报错）、布尔值和字符串
- 一等函数和闭包，包括返回函数的函数、带私有可变状态的闭包
- 通过命令行参数选择词法作用域或动态作用域
- `let`、`def`、赋值、`cond` 和代码块
- 内置函数：`add`、`sub`、`mul`、`eq`、`zero?`、`print`
- 遇到未绑定的名字、参数个数不对、类型错误、递归过深时给出清楚的错误信息，而不是直接崩溃

## 工作原理

```
program.417 ──► parse ──► JSON 语法树 ──► interpreter ──► 结果
```

语法分析由一个单独的程序完成，即 417 parser（见[致谢](#致谢)）。它把源代码转换成 JSON 格式的语法树，解释器用 [Jansson](https://github.com/akheron/jansson) 库从标准输入读取这棵树并求值。

- **环境。** 每个作用域是一串绑定，再加一个指向外层作用域的指针。查找名字时由内向外逐层查找。
- **闭包。** 对 `λ` 求值会生成一个函数值，里面保存着创建它时所在环境的指针。
- **词法作用域与动态作用域。** 两者的区别只在 `apply_function` 里的一行代码：函数调用时新建的作用域，是接在函数保存的环境后面（词法），还是接在调用者的环境后面（动态）。
- **内存管理。** 闭包可能比创建它的那次调用活得更久（比如返回函数的函数），所以不能在调用返回时释放环境。所有运行时对象在分配时都会被记录下来，程序结束时统一释放。用 AddressSanitizer 跑完整个测试集，没有报告内存错误或泄漏。
- **安全限制。** 整数运算会检查溢出；递归超过 5000 层时报错，而不是让 C 的调用栈溢出。

## 快速开始

需要 C 编译器、`make`、`git` 和 Jansson 库。

| 平台 | 安装 |
| --- | --- |
| Ubuntu / Debian / WSL | `sudo apt install build-essential git libjansson-dev` |
| macOS（Homebrew） | `brew install jansson` |
| Windows（MSYS2 UCRT64） | `pacman -S make git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-jansson` |

然后编译解释器、下载并编译 parser，再运行测试：

```bash
make            # 生成 ./interpreter
make parser     # 下载并编译 ./parse
make test
```

在 macOS 上需要告诉编译器 Homebrew 的路径：`make CPPFLAGS=-I$(brew --prefix)/include LDFLAGS=-L$(brew --prefix)/lib`。

已在 Ubuntu 22.04 上测试。在 Windows 上最省事的方式是用 WSL。

## 使用方法

```bash
./parse < examples/closures.417 | ./interpreter            # 词法作用域（默认）
./parse < examples/closures.417 | ./interpreter --dynamic  # 动态作用域
```

解释器会先输出程序里 `print` 打印的内容，最后输出整个程序的值。出错时输出以 `Error:` 开头的信息，并以状态码 1 退出。

## 语言说明

| 形式 | 例子 | 含义 |
| --- | --- | --- |
| 字面量 | `42`、`"hello"` | 整数或字符串 |
| 函数 | `λ(a, b) { add(a, b) }` | 创建一个函数（也可以写 `lambda`） |
| 调用 | `f(1, 2)` | 调用函数 |
| 代码块 | `{ e1; e2; e3 }` | 按顺序求值，值为最后一个表达式的值 |
| Let | `let x = 1; ...` | 在代码块剩下的部分绑定 `x` |
| Def | `def x = 1` | 把 `x` 加到当前作用域 |
| 赋值 | `x = 2` | 修改已有的绑定 |
| Cond | `cond (test => a) (true => b)` | 取第一个条件为真的分支的值 |

内置函数：`add`、`sub`、`mul`（整数）、`eq`（比较两个整数、布尔值或字符串）、`zero?` 和 `print`。初始环境里还绑定了 `true`、`false`、`x = 10`、`v = 5` 和 `i = 1`。

## 示例

| 文件 | 演示内容 | 词法作用域 | 动态作用域 |
| --- | --- | --- | --- |
| `scope.417` | 同一个程序在两种作用域规则下的结果 | `11` | `101` |
| `closures.417` | 返回函数的函数 | `15` | 报错 |
| `counter.417` | 带私有可变状态的闭包 | `3` | 报错 |
| `factorial.417` | 通过赋值实现递归 | `3628800` | `3628800` |
| `self_application.417` | 把函数传给自己来实现递归 | `3628800` | `3628800` |
| `fibonacci.417` | 通过 `def` 实现递归 | `6765` | `6765` |
| `strings.417` | 字符串、`print` 和 `eq` | `hello` `417` `true` | 相同 |

## 测试

`make test` 会运行 `tests/run_tests.sh`：每个示例在两种作用域下各跑一遍，再加上覆盖每个内置函数和每类错误的小程序，共 33 项检查。

## 目录结构

```
interpreter.c        解释器（值、环境、内置函数、求值器）
examples/            示例程序
tests/run_tests.sh   测试脚本
Makefile             编译、下载 parser、运行测试
```

## 致谢

417 语言和它的 parser 由 Jamie A. Jennings 编写，以 MIT 许可证发布在 [gitlab.com/JamieTheRiveter/csc417](https://gitlab.com/JamieTheRiveter/csc417)。`make parser` 会下载 parser，本仓库不包含它的代码。
