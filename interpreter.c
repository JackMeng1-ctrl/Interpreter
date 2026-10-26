// interpreter.c
//
// A tree-walking interpreter for the "417" language: a small,
// expression-oriented language with first-class functions, closures,
// conditionals, let/def bindings and assignment. It can run programs with
// either lexical (static) or dynamic scope.
//
// Programs are not parsed here. The interpreter reads the JSON abstract
// syntax tree produced by the 417 parser (see README.md) on stdin:
//
//     ./parse < program.417 | ./interpreter              lexical scope
//     ./parse < program.417 | ./interpreter --dynamic    dynamic scope

#define _POSIX_C_SOURCE 200809L

#include <jansson.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Deep recursion in a 417 program would otherwise overflow the C stack and
// crash; past this many nested calls we stop with an error instead.
#define MAX_CALL_DEPTH 5000

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

// Every error is fatal: one line on stderr and exit status 1.
static _Noreturn void fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("Error: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

// Closures can capture an environment that outlives the call that created
// it, e.g. a function that returns another function. Freeing a call's
// environment when the call returns would leave such closures pointing at
// freed memory. So every runtime object (values, environments, strings)
// is allocated here and stays alive until the program ends, when it is all
// released at once.

typedef union Allocation {
    union Allocation *next;
    max_align_t align;
} Allocation;

static Allocation *allocations = NULL;

static void *xalloc(size_t size) {
    Allocation *a = malloc(sizeof(Allocation) + size);
    if (!a) fail("out of memory");
    a->next = allocations;
    allocations = a;
    return a + 1;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = xalloc(n);
    memcpy(copy, s, n);
    return copy;
}

static void free_all(void) {
    while (allocations) {
        Allocation *next = allocations->next;
        free(allocations);
        allocations = next;
    }
}

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    TYPE_INT,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_FUNC,
    TYPE_BUILTIN_FUNC
} ValueType;

typedef enum {
    SCOPING_LEXICAL,
    SCOPING_DYNAMIC
} ScopingMode;

typedef struct Value Value;
typedef struct Env Env;

typedef struct {
    const char *name;
    size_t arity;
    Value *(*apply)(Value **args);
} Builtin;

// Values are immutable once created, so they can be shared freely between
// environments without copying.
struct Value {
    ValueType type;
    union {
        long long int_value;
        bool bool_value;
        const char *string_value;
        struct {
            const char **params;
            size_t param_count;
            json_t *body;  // the function's Block
            Env *env;      // environment where the lambda was evaluated
        } func_value;
        const Builtin *builtin;
    } data;
};

static Value TRUE_VALUE = { .type = TYPE_BOOL, .data.bool_value = true };
static Value FALSE_VALUE = { .type = TYPE_BOOL, .data.bool_value = false };

static Value *make_value(ValueType type) {
    Value *v = xalloc(sizeof *v);
    v->type = type;
    return v;
}

static Value *make_int(long long n) {
    Value *v = make_value(TYPE_INT);
    v->data.int_value = n;
    return v;
}

static Value *make_bool(bool b) {
    return b ? &TRUE_VALUE : &FALSE_VALUE;
}

static Value *make_string(const char *s) {
    Value *v = make_value(TYPE_STRING);
    v->data.string_value = xstrdup(s);
    return v;
}

static const char *type_name(ValueType type) {
    switch (type) {
        case TYPE_INT: return "an integer";
        case TYPE_BOOL: return "a boolean";
        case TYPE_STRING: return "a string";
        default: return "a function";
    }
}

static void print_value(const Value *v) {
    switch (v->type) {
        case TYPE_INT: printf("%lld", v->data.int_value); break;
        case TYPE_BOOL: fputs(v->data.bool_value ? "true" : "false", stdout); break;
        case TYPE_STRING: fputs(v->data.string_value, stdout); break;
        case TYPE_FUNC: fputs("<function>", stdout); break;
        case TYPE_BUILTIN_FUNC: printf("<builtin %s>", v->data.builtin->name); break;
    }
}

/* ------------------------------------------------------------------ */
/* Environments                                                        */
/* ------------------------------------------------------------------ */

typedef struct Binding {
    const char *name;
    Value *value;
    struct Binding *next;
} Binding;

// One scope: its own bindings (newest first) plus a link to the
// enclosing scope.
struct Env {
    Binding *bindings;
    Env *parent;
};

static Env *make_env(Env *parent) {
    Env *env = xalloc(sizeof *env);
    env->bindings = NULL;
    env->parent = parent;
    return env;
}

static void define(Env *env, const char *name, Value *value) {
    Binding *b = xalloc(sizeof *b);
    b->name = xstrdup(name);
    b->value = value;
    b->next = env->bindings;
    env->bindings = b;
}

static Binding *find_binding(Env *env, const char *name) {
    for (Env *e = env; e; e = e->parent) {
        for (Binding *b = e->bindings; b; b = b->next) {
            if (strcmp(b->name, name) == 0) return b;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Built-in functions                                                  */
/* ------------------------------------------------------------------ */

static long long int_arg(const char *fn, Value **args, size_t i) {
    if (args[i]->type != TYPE_INT) {
        fail("'%s' expects integer arguments, got %s", fn, type_name(args[i]->type));
    }
    return args[i]->data.int_value;
}

// Integer arithmetic is checked: overflowing a 64-bit integer is an error,
// not undefined behavior.
static Value *builtin_add(Value **args) {
    long long result;
    if (__builtin_add_overflow(int_arg("add", args, 0), int_arg("add", args, 1), &result)) {
        fail("integer overflow in 'add'");
    }
    return make_int(result);
}

static Value *builtin_sub(Value **args) {
    long long result;
    if (__builtin_sub_overflow(int_arg("sub", args, 0), int_arg("sub", args, 1), &result)) {
        fail("integer overflow in 'sub'");
    }
    return make_int(result);
}

static Value *builtin_mul(Value **args) {
    long long result;
    if (__builtin_mul_overflow(int_arg("mul", args, 0), int_arg("mul", args, 1), &result)) {
        fail("integer overflow in 'mul'");
    }
    return make_int(result);
}

static Value *builtin_eq(Value **args) {
    Value *a = args[0], *b = args[1];
    if (a->type != b->type) {
        fail("'eq' expects two values of the same type, got %s and %s",
             type_name(a->type), type_name(b->type));
    }
    switch (a->type) {
        case TYPE_INT: return make_bool(a->data.int_value == b->data.int_value);
        case TYPE_BOOL: return make_bool(a->data.bool_value == b->data.bool_value);
        case TYPE_STRING: return make_bool(strcmp(a->data.string_value, b->data.string_value) == 0);
        default: fail("'eq' cannot compare functions");
    }
}

static Value *builtin_zero(Value **args) {
    return make_bool(int_arg("zero?", args, 0) == 0);
}

static Value *builtin_print(Value **args) {
    print_value(args[0]);
    putchar('\n');
    return &TRUE_VALUE;
}

static const Builtin BUILTINS[] = {
    { "add", 2, builtin_add },
    { "sub", 2, builtin_sub },
    { "mul", 2, builtin_mul },
    { "eq", 2, builtin_eq },
    { "zero?", 1, builtin_zero },
    { "print", 1, builtin_print },
};

static Env *make_global_env(void) {
    Env *env = make_env(NULL);
    for (size_t i = 0; i < sizeof BUILTINS / sizeof BUILTINS[0]; i++) {
        Value *fn = make_value(TYPE_BUILTIN_FUNC);
        fn->data.builtin = &BUILTINS[i];
        define(env, BUILTINS[i].name, fn);
    }
    // The language's initial environment also binds these names.
    define(env, "x", make_int(10));
    define(env, "v", make_int(5));
    define(env, "i", make_int(1));
    define(env, "true", &TRUE_VALUE);
    define(env, "false", &FALSE_VALUE);
    return env;
}

/* ------------------------------------------------------------------ */
/* Evaluator                                                           */
/* ------------------------------------------------------------------ */

static ScopingMode scoping = SCOPING_LEXICAL;
static int call_depth = 0;

static Value *evaluate(json_t *expr, Env *env);

// Checks that an AST node is an array of `count` items (any size if 0).
static json_t *form_items(json_t *items, const char *form, size_t count) {
    if (!json_is_array(items) || (count && json_array_size(items) != count)) {
        fail("malformed %s in the AST", form);
    }
    return items;
}

static const char *identifier_name(json_t *node, const char *what) {
    json_t *id = json_is_object(node) ? json_object_get(node, "Identifier") : NULL;
    if (!json_is_string(id)) fail("%s must be an identifier", what);
    return json_string_value(id);
}

static Value *lookup(const char *name, Env *env) {
    Binding *b = find_binding(env, name);
    if (!b) fail("unbound identifier '%s'", name);
    return b->value;
}

static Value *apply_function(Value *fn, Value **args, size_t argc, Env *caller_env) {
    if (fn->type == TYPE_BUILTIN_FUNC) {
        const Builtin *b = fn->data.builtin;
        if (argc != b->arity) {
            fail("'%s' expects %zu argument%s, got %zu", b->name, b->arity,
                 b->arity == 1 ? "" : "s", argc);
        }
        return b->apply(args);
    }
    if (fn->type != TYPE_FUNC) fail("cannot call %s", type_name(fn->type));

    size_t expected = fn->data.func_value.param_count;
    if (argc != expected) {
        fail("function expects %zu argument%s, got %zu", expected, expected == 1 ? "" : "s", argc);
    }
    if (++call_depth > MAX_CALL_DEPTH) {
        fail("recursion too deep (more than %d nested calls)", MAX_CALL_DEPTH);
    }

    // The only difference between the two scoping modes: which environment
    // the function body's new scope extends.
    //   lexical: the environment where the lambda was evaluated (its closure)
    //   dynamic: the environment of the caller
    Env *parent = scoping == SCOPING_LEXICAL ? fn->data.func_value.env : caller_env;
    Env *call_env = make_env(parent);
    for (size_t i = 0; i < argc; i++) {
        define(call_env, fn->data.func_value.params[i], args[i]);
    }
    Value *result = evaluate(fn->data.func_value.body, call_env);
    call_depth--;
    return result;
}

static Value *eval_application(json_t *items, Env *env) {
    form_items(items, "Application", 0);
    if (json_array_size(items) == 0) fail("malformed Application in the AST");

    Value *fn = evaluate(json_array_get(items, 0), env);
    size_t argc = json_array_size(items) - 1;
    Value **args = malloc((argc ? argc : 1) * sizeof *args);
    if (!args) fail("out of memory");
    for (size_t i = 0; i < argc; i++) {
        args[i] = evaluate(json_array_get(items, i + 1), env);
    }
    Value *result = apply_function(fn, args, argc, env);
    free(args);
    return result;
}

static Value *eval_lambda(json_t *items, Env *env) {
    form_items(items, "Lambda", 2);
    json_t *params = json_object_get(json_array_get(items, 0), "Parameters");
    json_t *body = json_array_get(items, 1);
    if (!json_is_array(params)) fail("malformed lambda parameters in the AST");
    if (!json_is_object(body) || !json_object_get(body, "Block")) {
        fail("a lambda's body must be a block");
    }

    size_t count = json_array_size(params);
    const char **names = xalloc((count ? count : 1) * sizeof *names);
    for (size_t i = 0; i < count; i++) {
        names[i] = xstrdup(identifier_name(json_array_get(params, i), "a lambda parameter"));
        for (size_t j = 0; j < i; j++) {
            if (strcmp(names[i], names[j]) == 0) fail("duplicate parameter '%s'", names[i]);
        }
    }

    Value *fn = make_value(TYPE_FUNC);
    fn->data.func_value.params = names;
    fn->data.func_value.param_count = count;
    fn->data.func_value.body = body;
    fn->data.func_value.env = env;
    return fn;
}

static bool clause_matches(json_t *test, Env *env) {
    // A test written as "(name, number)" reaches us as a parameter list.
    // It is shorthand for eq(name, number).
    json_t *pair = json_is_object(test) ? json_object_get(test, "Parameters") : NULL;
    if (pair) {
        if (!json_is_array(pair) || json_array_size(pair) != 2) {
            fail("a (name, number) cond test must have exactly two parts");
        }
        Value *v = lookup(identifier_name(json_array_get(pair, 0), "the first part of a (name, number) cond test"), env);
        json_t *number = json_array_get(pair, 1);
        if (v->type != TYPE_INT || !json_is_integer(number)) {
            fail("a (name, number) cond test compares an integer variable with an integer");
        }
        return v->data.int_value == json_integer_value(number);
    }

    Value *v = evaluate(test, env);
    if (v->type != TYPE_BOOL) fail("a cond test must be a boolean, got %s", type_name(v->type));
    return v->data.bool_value;
}

static Value *eval_cond(json_t *clauses, Env *env) {
    form_items(clauses, "Cond", 0);
    for (size_t i = 0; i < json_array_size(clauses); i++) {
        json_t *clause = json_object_get(json_array_get(clauses, i), "Clause");
        form_items(clause, "cond clause", 2);
        if (clause_matches(json_array_get(clause, 0), env)) {
            return evaluate(json_array_get(clause, 1), env);
        }
    }
    fail("no cond clause matched");
}

static Value *eval_assignment(json_t *items, Env *env) {
    form_items(items, "Assignment", 2);
    const char *name = identifier_name(json_array_get(items, 0), "the target of an assignment");
    Binding *b = find_binding(env, name);
    if (!b) fail("assignment to unbound identifier '%s'", name);
    Value *value = evaluate(json_array_get(items, 1), env);
    b->value = value;
    return value;
}

// let name = value { block }: the binding is visible only inside the block.
static Value *eval_let(json_t *items, Env *env) {
    form_items(items, "Let", 3);
    const char *name = identifier_name(json_array_get(items, 0), "the name in a let");
    Value *value = evaluate(json_array_get(items, 1), env);
    Env *scope = make_env(env);
    define(scope, name, value);
    return evaluate(json_array_get(items, 2), scope);
}

// def name = value: adds the binding to the current scope.
static Value *eval_def(json_t *items, Env *env) {
    form_items(items, "Def", 2);
    const char *name = identifier_name(json_array_get(items, 0), "the name in a def");
    Value *value = evaluate(json_array_get(items, 1), env);
    define(env, name, value);
    return value;
}

// A block evaluates its expressions in order; its value is the last one.
static Value *eval_block(json_t *exprs, Env *env) {
    form_items(exprs, "Block", 0);
    size_t count = json_array_size(exprs);
    if (count == 0) fail("an empty block has no value");
    Value *last = NULL;
    for (size_t i = 0; i < count; i++) {
        last = evaluate(json_array_get(exprs, i), env);
    }
    return last;
}

static Value *evaluate(json_t *expr, Env *env) {
    if (json_is_integer(expr)) return make_int(json_integer_value(expr));
    if (json_is_string(expr)) return make_string(json_string_value(expr));
    if (json_is_true(expr)) return &TRUE_VALUE;
    if (json_is_false(expr)) return &FALSE_VALUE;
    if (!json_is_object(expr) || json_object_size(expr) != 1) {
        fail("malformed expression in the AST");
    }

    // Every other node is an object with a single key naming the form.
    void *iter = json_object_iter(expr);
    const char *form = json_object_iter_key(iter);
    json_t *body = json_object_iter_value(iter);

    if (strcmp(form, "Identifier") == 0) {
        if (!json_is_string(body)) fail("malformed Identifier in the AST");
        return lookup(json_string_value(body), env);
    }
    if (strcmp(form, "Application") == 0) return eval_application(body, env);
    if (strcmp(form, "Lambda") == 0) return eval_lambda(body, env);
    if (strcmp(form, "Cond") == 0) return eval_cond(body, env);
    if (strcmp(form, "Assignment") == 0) return eval_assignment(body, env);
    if (strcmp(form, "Let") == 0) return eval_let(body, env);
    if (strcmp(form, "Def") == 0) return eval_def(body, env);
    if (strcmp(form, "Block") == 0) return eval_block(body, env);
    fail("unsupported expression type '%s'", form);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static json_t *program = NULL;

static void cleanup(void) {
    json_decref(program);
    free_all();
}

static void usage(FILE *out, const char *prog) {
    fprintf(out,
            "Usage: %s [--lexical | --dynamic]\n"
            "\n"
            "Reads a program's JSON syntax tree (output of the 417 parser) on stdin,\n"
            "runs it, and prints the value of the program.\n"
            "\n"
            "  --lexical   use lexical (static) scope (default)\n"
            "  --dynamic   use dynamic scope\n"
            "  -h, --help  show this help\n",
            prog);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dynamic") == 0) {
            scoping = SCOPING_DYNAMIC;
        } else if (strcmp(argv[i], "--lexical") == 0) {
            scoping = SCOPING_LEXICAL;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else {
            usage(stderr, argv[0]);
            return 1;
        }
    }

    atexit(cleanup);

    // JSON_DECODE_ANY: a program can be a bare number or string, not only
    // an object.
    json_error_t error;
    program = json_loadf(stdin, JSON_DECODE_ANY, &error);
    if (!program) {
        fail("invalid JSON input (line %d, column %d): %s", error.line, error.column, error.text);
    }

    Value *result = evaluate(program, make_global_env());
    print_value(result);
    putchar('\n');
    return 0;
}
