# mscript at a glance

mscript is the language inside `.mdbv` view files. It's a small, read-only subset of C++ for walking a program's
data structures from the debugger: C++ expressions, plus statements and functions for computing a value's summary
and children. This page lists what it can do. [views.md](views.md) is the full guide, with examples.

## Where it runs

A view is a set of items for the types its pattern matches:

| Item | Runs |
|---|---|
| `using T = …` | at bind time: names a type |
| `let x = …` | at most once per displayed value, the first time it's used |
| `fn name(…) …` | when called |
| `summary …` | every time the value is displayed |
| `children …` | when the value is expanded, one page at a time |

## Expressions

- Members by name (`mLength`) or through `this`. Then `a.b`, `a->b`, `a[i]`, `*p`, `&a`.
- Arithmetic, bitwise, comparison and logical operators, and `?:`. `&&`, `||` and `?:` don't evaluate the side they
  skip, so `node && node->mValue` is safe.
- Pointer arithmetic (`hdr + 1`, `end - begin` in elements).
- Casts to builtin types, pointers, `using` names and named types.
- `sizeof`, and literals: integers, floats, characters, strings, `true`/`false`, `nullptr`.
- Integer arithmetic is done in 64 bits. It is unsigned only when an operand is a 64-bit unsigned type.
- Builtins:
  - `template_arg(N)`, and `typeof(expr)` in a `using`;
  - `text`, `cstr`, `utf8`/`utf16`/`utf32` for strings, with `limit:` (4096 code units by default);
  - `array(ptr, count)` for contiguous children, with `limit:` (1,000,000 by default).
- Summary templates: `"len={mLength} flags={mFlags,x}"`. The formats are `x`, `X`, `d`, `o`, `b` and `c`.

## Statements

They're allowed in `children { … }` blocks and in function bodies:

- blocks `{ … }`;
- `type name = expr;` and `auto name = expr;`;
- `=`, `+=`, `-=`, `++`, `--`;
- `if`/`else`, `while`, `for (init; cond; step)`, where each part of a `for` may be empty;
- `break;`, `continue;`, `return;` and `return expr;`;
- `yield value;` and `yield [key] value;`;
- calls as statements: `walk(n->mLeft);`.

**Locals** hold numbers and pointers only. They are the only thing a script can assign: a view never changes the
program.

## Children

| Form | Children |
|---|---|
| `children default`, or no `children` | the type's own members |
| `children none` | none: the value can't be expanded |
| `children array(ptr, count)` | `count` elements next to each other in memory |
| `children (size: n, limit: N) { … }` | whatever the block yields. Both options are optional |
| `children (size: n) walk(root)` | whatever the void function `walk` yields |

- **`size`** is evaluated without running the block. It gives the UI the element count, so the UI can page.
- **`limit`** ends the children after N elements, with an error child. It guards against corrupt, looping lists.
- **Paging:** mdb remembers where a block was every 100 children, so later pages don't start over.
- **`yield` takes two kinds of value:**
  - an object in memory (`*entry`, `node->mValue`), which is displayed with its own view and can be expanded;
  - a computed number (`(bool)(word >> bit & 1)`). It has no address, and its type must exist in the program's debug
    information.
- **Keys:** `yield [key] value` names the child by the key. Without a key, children are named `[0]`, `[1]`, ….

## Functions

```
fn count(Node *n) -> u64 { … }   // value function: returns a number or pointer, can't yield
fn walk(Node *n) { … }           // void function: can yield, is called as a statement
```

- **Value functions** can be called from any expression: `summary`, `let`, `size`, conditions, other functions.
- **Void functions** can be called from a `children` block or another void function, or used as the `children` item.
- Functions can be written in any order, can be recursive, and can call each other. They see the view's members,
  `using`s and `let`s.
- **Reference parameters** (`u64 &acc`) let a function update a local of its caller. Only a local, or another
  reference parameter, can be passed to one.
- Calls can nest 1024 deep.

## Limits and errors

| What | Limit |
|---|---|
| a request for children | 1,000,000 steps |
| each summary part, `let` and `size` | 100,000 steps |
| call depth | 1024 |

When a limit is hit, or the program's memory can't be read, the value shows `<view error: file:line:col: …>`, or the
children end with a `[view error]` child. The rest of the variables panel is unaffected.

## Not supported

- Calling functions in the program being debugged. A view can only read memory.
- Functions shared between views, and calls from one view's functions into another's.
- Locals that hold text, structs or arrays, and containers such as a visited set.
- Delegation (a `RefPtr<T>` displayed as its `T`), nested summaries, and `template_arg(i, j)`.
- Watch expressions, hover and breakpoint conditions in mscript. They're planned, and still use JavaScript today.
