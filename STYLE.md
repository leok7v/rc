Copyright (c) 2024, "Leo" Dmitry Kuznetsov

# Code style

There is K&R C, ANSI C, C89, C99, C11, C18, C2x and there is "C as I like it".

There is language itself, preprocessor, ancient runtime, several bickering
standardization committees corporate and open source flavors and plethora of
coding styles (e.g. MISRA C).

I have chosen to stick to the "C as I like it" style because it pleases _my_
eye as long as it does not offend wider audience too much. I am aware that
it is not the most popular style. I listen to critics and I am willing to
improve the code to make it more readable and more portable.

# Frequent points of disagreement

* '''if (foo) { bar(); }''' without putting the compound block at a line 
  of its own might be considered less readable.
* using ```enum {}``` for constants is at mercy of compiler ```int``` type
  bitwidth but at least it does not leak names into global
  scope as ```#define``` does.
* The code is not always suitable for 16 and 8 bit architectures some tweaks 
  may be required.
* ```#define CONSTANTS_IN_SHOUTING_ALL_CAPS``` is a great tradition but not
  a requirement. As a person, who has first hand experience with FORTRAN
  in 1976, I beg you pardon for not using ALL_CAPS for constants.
* ```#define null ((void*)0)``` I personally invented this decades before C++
  nullptr was even discussed. It's mine and I will stick to it till C29 will
  have nullptr.
* IMHO ```countof()``` should be part of the language. P}ay close attention
  to the fact that it should not be used on arrays decaying to pointers.
* ```rt__assert(bool, "printf formatted message", argv)``` is super easy to
  implement and super useful in debugging. If you don't like don't use it.
* ```swear(bool, "printf formatted message", argv)``` is a release mode
  fail-fast fatal assertion.

# Single header file libraries

Why do I like single header file libraries:

* less is more: a single file to include and compile
* easier to maintain, distribute, read, understand, debug...
* room for interesting inline for performance optimizations
* machine-code bloat (arguably) 
  https://en.wikipedia.org/wiki/Header-only
  can be resoled easily by separating implementation includes
  into a single compilation unit (e.g. implementations.c).
* brittleness and longer compilation times only applies to 
  huge codebases which header and again can be mitigated by
  additional .c compilation unit per header.
* the "harder to understand" argument probably invented by
  gullible and trusting audience.
* comparing to binary libraries distribution much easier
  to tune compilation flavors (e.g. Release with Debug Information).

# Namespaces

Because C does not have namespaces two letters "ns_"-like prefixes
are used instead. Global replace makes it easier to rename in
fully open source projects. Close source projects will be forced
to use shims and local compilation unit scopes on namespaces 
collisions.

Use of suffixes like `_t` for typedefs and `_e` for `enums` is a bit conflicted:

[POSIX and ISO-9899 on Namespaces](https://stackoverflow.com/q/37369400/665792)

[POSIX and ISO-9899 on suffixes](https://stackoverflow.com/a/56936803/665792)

[IEEE Std 1003.1](https://pubs.opengroup.org/onlinepubs/009695399/xrat/xsh_chap02.html)

[ISO-9899](https://stackoverflow.com/questions/56935852/does-the-iso-9899-standard-has-reserved-any-use-of-the-t-suffix-for-identifiers)

# Controversial

* `fp32_t` for `float`
* `fp64_t` for `double`
with possible future extensions:
* `fp8_t`  for 8-bit float
* `fp16_t` for 16-bit float
* `bf16_t` for [bfloat16](https://en.wikipedia.org/wiki/Bfloat16_floating-point_format) 

# Structs versus typedefs

`struct` usage in C is more verbose because the explicit `struct` 
declarations every time a compound type was used. 
Over time, developers began to use `typedef` to reduce this verbosity, 
leading to the common pattern of `typedef struct { ... } typename;` 
to avoid having to prepend `struct` everywhere.

However, in recent years, there has been a growing movement to avoid 
`typedef` overuse, especially when it hides crucial details of a type.

- **Use `struct` for compound types** when you want to emphasize the 
  composite nature of the data and when keeping the global namespace 
  clean is a priority. It also provides self-documenting code for 
  complex types.
- **Use `typedef` for atomic types** or when simplifying overly 
  complex type definitions such as function pointers or array types.
  It can also be helpful for abstracting platform-specific details 
  in a clean and portable way.

Ultimately, the decision to use `struct` or `typedef` should be based 
on the context of the project, balancing readability, potential for 
namespace collisions, and historical practices. Each has its place.

# Reference parameters and pointer type

```c
struct bar {
    int member;
};

void foo(struct bar* *b) {
    static struct bar singleton;
    *b = &singleton;
}
```

`struct bar*` is a _pointer type_ and asterisk is part of the type name
while `*b` is _reference parameter_ and asterisk prexing paremeter name
indicates that.Compare to C++:

```c++
class bar {
    void foo(bar* &b) {
        static bar singleton;
        b = &singleton;
    }
    int member;
};
```

# Callbacks and `functors`

```c
struct foo {
    void (*callback)(struct foo*, ...);
};

struct bar {
    union { // Always first member
        struct foo base;
        struct foo;
    };
    int extra;
};

// Anonymous struct foo alias to base is not necessary.
// It is advised to facilitate pushing member names up the scope
// to prevent extended type name collision wiht base type
// that may leads to usage confusion and error-prone hard to 
// understand code.

void foo_callback(struct foo* f, ...) {
    printf("foo_callback\n");
}

void bar_callback(struct foo* f, ...) {
    struct bar* b = (struct bar*)f; // Cast to extended type
    printf("bar_callback  extra: %d\n", b->extra);
}

void foo_op(struct foo* f) {
    f->callback(f);
}

int main() {
    struct foo foo;
    foo.callback = foo_callback;
    struct bar bar;
    bar.callback = foo_callback;
    bar.extra = 153;
    foo_op(&foo);
    foo_op(&bar.base);
    return 0;
}
```

# Modern C _Generic polymorphism

Could be useful for min()/max()/swap() side effects of double evaluation of arguments.

See [rt_generics.h](rt_generics.h)
