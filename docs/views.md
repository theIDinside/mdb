# Views: telling mdb how to display your types

A *view* describes how values of a type are displayed in the variables panel:

- what goes in the one-line value next to the variable (the **summary**), and
- what you see when you expand it (the **children**).

Views are written in small `.mdbv` files, using C++ expressions:

```
// Gecko's nsTArray: the elements live right after the header mHdr points to.
view nsTArray_Impl<.*> in "libxul.so" {
  using T = template_arg(0)
  let hdr = mHdr;
  summary "length={hdr->mLength}"
  children array((T*)(hdr + 1), hdr->mLength)
}

// nsString, nsCString, nsAutoString, ... all derive from nsTStringRepr.
view mozilla::detail::nsTStringRepr<.*> {
  summary text(mData, mLength)
  children none
}
```

With these loaded, an `AutoTArray<nsCString, 4>` holding two strings is displayed as `length=2`, and expanding
it shows `[0] "foo"`, `[1] "bar"`: `AutoTArray` gets the view of its base class, and each element is displayed by
the string view.

## Loading views

- From the debug console: `loadViews("/path/to/gecko.mdbv")`. Loading the same file again reloads it, so you can
  edit a view and see the result right away.
- In the launch configuration: `"views": ["/path/to/gecko.mdbv"]`.
- Automatically, at startup: every `*.mdbv` file in `$MDB_VIEWS_DIR`, or `$XDG_CONFIG_HOME/mdb/views`, or
  `~/.config/mdb/views`.

Problems in a view file are reported, but don't stop the rest of the file from loading. `loadViews` returns the
diagnostics, and the launch request prints them to the debug console.

## Writing a view

```
view <pattern> [in "<object file>"] {
  using <Name> = <type>;            // any number
  let <name> = <expression>;        // any number
  fn <name>(<parameters>) [-> <type>] { ... } // any number
  summary <template or expression>  // optional
  children <none | default | array(...) | { ... }> // optional
}
```

Semicolons after items are optional. Comments are `// ...` and `/* ... */`. `using` and `let` declarations can
use the ones written before them.

### Patterns: which types a view applies to

The pattern is a regular expression (ECMAScript syntax), written right after `view`. It ends at the first
whitespace, so leave a space before `{`. If a pattern needs a space, quote it: `view "\(anonymous namespace\)::Foo"`.
Inside quotes, backslashes are regular expression escapes, so the parentheses are escaped here.

The pattern must match the type's whole name (it is implicitly anchored with `^…$`), so `nsTArray<.*>` does not
match `nsTArray_Impl<int>`. Names look like this:

- Fully qualified, without inline namespaces: `std::vector<int,std::allocator<int>>`, not
  `std::__1::vector<...>`. A type in an anonymous namespace is `(anonymous namespace)::Foo`.
- Whitespace is removed, except between two words: `std::map<long unsigned int,const char*>`.
- The template arguments are spelled the way the compiler wrote them. gcc writes `long unsigned int` where clang
  writes `unsigned long`, so prefer `.*` over spelling arguments out. With `-gsimple-template-names` the compiler
  leaves the arguments out of the name entirely; mdb rebuilds them from the debug information, so the same pattern
  still matches.

| Pattern | Matches |
|---|---|
| `Plain` | the type `Plain` in the global namespace |
| `std::vector<.*>` | every `std::vector` |
| `mozilla::detail::nsTStringRepr<(char\|char16_t)>` | those two instantiations |
| `nsC?String` | `nsString` and `nsCString` |

The mdb log (`core` channel) shows the name of every type that no view matched: `[views] no view for 'Foo<int>'`.

A view also applies to every class derived from the type it matches, unless the derived class has a view of its
own. When several views match the same type, the one loaded last wins.

`in "libxul.so"` restricts the view to types defined in that object file (file name or full path).

### Types: `using`

`using` names a type, for casts and `sizeof`:

| Declaration | |
|---|---|
| `using T = template_arg(0)` | the matched type's first template argument, read from the debug information |
| `using Item = typeof(*mBegin)` | the type of an expression. The expression is not evaluated. |
| `using Header = nsTArrayHeader` | a named type, a builtin type, or a pointer to one (`using P = T*`) |

In an expression, `template_arg(N)` is the value of a template *value* argument, like the `N` of
`AutoTArray<T, N>`:

```
view FixedBuffer<.*> {
  using T = template_arg(0)
  summary "{mUsed} of {template_arg(1)}"
  children array((T*)mItems, mUsed)
}
```

### Expressions

Expressions are C++ expressions evaluated against the value being displayed. Members are named directly (`mLength`),
or through `this` (`this->mLength`).

- member access `a.b`, `a->b`, indexing `a[i]`, `*p`, `&a`
- arithmetic, bitwise, comparison and logical operators, `?:`
- pointer arithmetic: `hdr + 1`, `end - begin` (in elements)
- casts to builtin types, pointers, `using` names and named types: `(T*)p`, `(unsigned long)x`,
  `(Header*)p`
- `sizeof(T)`, `sizeof(expr)`
- literals: `42`, `0x2a`, `1.5`, `'a'`, `"text"`, `true`, `nullptr`
- `template_arg(N)` for template value arguments
- bitfields read as you'd expect, and C++ references are dereferenced automatically

All integer arithmetic is done in 64 bits. It is unsigned only if an operand is a 64-bit unsigned type, so
`mEnd - mStart` doesn't wrap around when the members are `uint32_t`.

`let` names a value for use later in the view. A `let` can use the ones declared before it. Each `let` is evaluated
at most once per displayed value.

### Summaries

- A template string: `summary "length={hdr->mLength} capacity={hdr->mCapacity}"`. Each `{expression}` is replaced
  by its value. A format can follow a comma: `{mFlags,x}` (hex). The formats are `x`, `X`, `d`, `o`, `b` and `c`
  (character). Write `{{` and `}}` for literal braces.
- An expression: `summary text(mData, mLength)` or `summary mCount, x`.

Without a summary, the type name is shown, as it is for types without views.

### Text

| Function | |
|---|---|
| `text(ptr, length)` | `length` code units at `ptr`. The encoding follows the element type: 1 byte is UTF-8, 2 bytes UTF-16 (`char16_t`), 4 bytes UTF-32. |
| `text(ptr)`, `cstr(ptr)` | a NUL-terminated string |
| `utf8(ptr, len)`, `utf16(ptr, len)`, `utf32(ptr, len)` | force an encoding regardless of the pointer type |

All of them take `limit: N` (default 4096 code units). Longer text is cut off and marked with `...`. Invalid
sequences (bad UTF-8, unpaired surrogates) show as U+FFFD. A null pointer with length 0 is the empty string.

### Children

- `children default`, or no `children`: the type's own members, as for types without a view.
- `children none`: the value can't be expanded.
- `children array(ptr, count)`: `count` elements of the type `ptr` points to. The UI asks for them a page at a time,
  so large arrays are cheap. `limit: N` caps the count (default 1,000,000). The elements are displayed using their
  own views.
- `children { ... }`: produce children with a block of statements. See [Children blocks](#children-blocks).

### Children blocks

Use a block when your elements aren't stored in one contiguous array. This view walks a linked list and reports
its known length, so the UI can request pages of children:

```
view MdbList {
  children (size: mLength) {
    for (auto node = mHead; node; node = node->mNext)
      yield node->mValue;
  }
}
```

Write `children { ... }` or `children (size: expression, limit: N) { ... }`. Either option can be omitted, and
you can write them in either order. `size` is evaluated without running the block and must be an integer. It tells
the UI how many children to expect, so the UI can ask for them a page at a time. Without a size, the value is
expandable and the UI asks for all its children at once; mdb returns at most 1,000 children per request. Write a
size whenever your container knows its length. While paging through a block, mdb remembers its progress every
100 children, so later pages don't have to start at the beginning. That progress is cleared when the program
continues.

Inside the block, you can use:

- blocks `{ ... }`;
- declarations `type name = expression;` and `auto name = expression;` (an initializer is required);
- assignments `=`, `+=`, `-=`, `++` and `--` as statements, with `++` and `--` in prefix or postfix form;
- `if`/`else`, `while` and `for (init; condition; step)` loops. Any part of a `for` header can be empty;
- `continue;`, `break;` and `return;` (`return` ends the block);
- `yield value;` and `yield [key] value;` to add children.

Locals hold numbers and pointers. To refer to a struct, keep a pointer to it; trying to store a struct in a local
is an error. Locals are the only values you can assign. Assigning to a member of the object is an error: a view
never changes the program. A variable declared in a `for` initializer exists only inside that loop.

`yield` takes either an object in memory or a computed value. An object in memory, such as `*entry` or
`node->mValue`, is shown using its own view; structs can be expanded, and the child has a memory address. A
computed value has no address, so it can't be expanded. Its type must exist in the program's debug information;
cast it to a type the program uses, such as `u32`. This is useful for values such as individual bits in a bitset.
Use a key to name a child; keys are formatted like summary values. Without a key, children are named `[0]`,
`[1]`, and so on.

For example, this hash table view skips free or removed slots and names each child with its key:

```
view MdbHashMap<.*> {
  using Entry = template_arg(0)
  children (size: mTable.mEntryCount) {
    for (i64 slot = 0; slot < mTable.mCapacity; ++slot) {
      if (mTable.mHashes[slot] <= 1) continue;
      auto entry = (Entry*)(mTable.mEntries + slot * mTable.mEntrySize);
      yield [entry->mKey] *entry;
    }
  }
}
```

Each request for children can run up to 1,000,000 steps. If it runs out, the page ends with a `[view error]`
child saying `stopped after N steps`. Summaries, `let` values and sizes each have a 100,000-step limit. The
optional `limit: N` ends the children after N elements and adds a `[view error]` child saying `limit of N elements
reached`; use it to guard against a corrupt list that loops back on itself.

### Functions

A view can define value functions and void functions:

```
fn count(Node *node) -> u64 { ... }  // returns a number or pointer
fn walk(Node *node) { ... }          // can yield children
```

A value function has a return type and can't yield. Call it wherever an expression is allowed, such as in a
summary, a `let`, a `size`, or another function. A void function has no return type and can yield. Call it as a
statement from a children block or another void function, or use it directly as the children item:

```
children (size: mSize) walk(mRoot)
```

Functions can be written in any order, call themselves recursively, and call each other. A function can also see
the view's members, `using` declarations and `let` values. Parameters are passed by value unless declared as
references, such as `u64 &acc`. A reference parameter can only be given a local (or another reference parameter),
and lets a function update a caller's local or return more than one value. You can't pass a member or an expression
as a reference argument.

Calls can be nested up to 1024 levels. Going deeper is an error: a summary shows
`<view error: ...: call depth exceeded 1024>`, and while producing children, the page ends with a `[view error]`
child that says so. This also stops a walk over a corrupt tree whose nodes point back at each other.

For example, this view walks a binary search tree in key order and computes its size and sum:

```
view MdbTree {
  fn walk(MdbTreeNode *n) {
    if (!n) return;
    walk(n->mLeft);
    yield [n->mKey] n->mValue;
    walk(n->mRight);
  }
  fn count(MdbTreeNode *n) -> u64 {
    if (!n) return 0;
    return count(n->mLeft) + 1 + count(n->mRight);
  }
  fn sum(MdbTreeNode *n, u64 &acc) -> u64 {
    if (!n) return acc;
    acc += n->mValue;
    sum(n->mLeft, acc);
    sum(n->mRight, acc);
    return acc;
  }
  fn total() -> u64 {
    u64 acc = 0;
    sum(mRoot, acc);
    return acc;
  }
  let size = count(mRoot);
  summary "size={size} sum={total()}"
  children (size: mSize) walk(mRoot)
}
```

## When something goes wrong

A view that doesn't fit the type it matched is shown in place of the value, with the location in the view file.
For example, it might refer to a member that doesn't exist:

```
<view error: gecko.mdbv:3:20: no member named 'mHddr' in 'nsTArray_base<...>'>
```

The value's members can still be expanded. Errors while evaluating (unreadable memory, a null pointer) are shown
the same way. If a value's children can't be produced, expanding it shows a single `[view error]` child that
explains why. A block that runs too long, or yields more than its limit, ends with a `[view error]` child saying
so.

## Not yet supported

- Functions shared between views.
- Locals that hold text, records or fixed-size arrays, such as `u64 stack[64]`.
- Containers such as a visited set.
- Delegation and `template_arg(i, j)`.
- Named children next to the indexed ones, such as a `[length]` child before the elements.
- Nested summaries: a member with its own view shows as `{TypeName}` inside a summary.
- Template types written out in expressions or `using` (`(Foo<int>*)p`); use `template_arg(N)` or `typeof(...)`
  instead.
- Watch and hover expressions still go to the JavaScript console. They are planned to use the same expression
  language.
