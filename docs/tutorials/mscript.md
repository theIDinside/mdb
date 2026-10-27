# mscript: design and implementation

**mscript** is the language inside a view: the C-like expressions views started with, plus statements and
functions. View files are `.mdbv` files. This document records how mscript is designed and why, and how it was
built. The user-facing guide is [views.md](../views.md), and [mscript.md](../mscript.md) is a one-page feature
overview.

Everything is compiled to a **stack bytecode** that runs on a small VM. Functions can be recursive and take
reference parameters. Calling mscript from QuickJS is described in an appendix, and isn't implemented.

```
view MdbHashMap<.*> {
  using Entry = template_arg(0)
  summary "count={mTable.mEntryCount}"
  children (size: mTable.mEntryCount) {
    for (i64 slot = 0; slot < mTable.mCapacity; ++slot) {
      if (mTable.mHashes[slot] <= 1) continue;
      auto entry = (Entry*)(mTable.mEntries + slot * mTable.mEntrySize);
      yield [entry->mKey] *entry;
    }
  }
}

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
  summary "size={count(mRoot)}"
  children (size: mSize) walk(mRoot)
}
```

Paging works by pausing the program after a page's worth of `yield`s, and continuing it when the client asks for the
next page. That also works in the middle of a recursive walk, because the VM's call stack is data, not the C++
stack.

## Why a stack VM, and "no lifetimes"

A view must be able to stop anywhere (budget, page full) and continue later from a copy of where it stopped. The
whole state of a paused program is:

```
ProgramState { frames: [{function, pc, localBase}], locals: [values], operands: [values] }
```

That is three vectors of plain values. Copying them is cheap, so a checkpoint is just a copy. Two rules keep it
that way:

- **Values have no lifetimes.** A value is a scalar, a pointer into the target, or text, and it's copied when it's
  stored. There's no heap. References to locals exist only as parameters that point down the stack (decision 4),
  so nothing can outlive what it refers to.
- **Calls go one way.** mscript never calls out: not into JS, and never into the program being debugged. If JS
  integration is added (the optional appendix), JS can call mscript and receives what it produces, but mscript
  can't call JS. Otherwise the paused state would include things that can't be copied, and the step budget and the
  read-only guarantee would no longer hold.

Embedded languages (Lua, QuickJS, AngelScript) were considered. They give you `yield` through coroutines or
generators, but their paused state can't be copied. They also don't know about typed lvalues in another process,
so you lose `->`, casts and pointer arithmetic over target memory.

## Design decisions

### 1. A children block is a function

Internally it is one: the binder compiles the block into a `Function` named "children", with no parameters and
`mCanYield` set. Nothing in the VM or the runtime treats it specially. The only special parts are the header
(`size:` and `limit:`, which describe the *result* and not the code) and the shorthand syntax.

The language says so too:

```
children (size: mSize) { walk(mRoot); }    // shorthand: an anonymous void function
children (size: mSize) walk(mRoot)         // the same: a call to a void function
```

`children array(p, n)` stays as its own form. It isn't a generator: elements next to each other in memory are
served directly, without running anything. That's also why it can page without checkpoints.

### 2. Compile everything to a stack bytecode

Lets, summaries, `size` and `array(...)` run on the VM too, not only the children blocks.
- *Why:* calls can appear inside expressions, any instruction can pause, the budget counts every instruction, and
  there's one evaluator instead of two.
- *Cost:* `evaluator.cpp` (~670 lines) is rewritten as a VM loop.

The binder still produces typed `BoundNode` trees, and codegen flattens them. Type errors stay in the binder, and
the existing binder tests stay valid.

### 3. Values have no lifetimes

The paused state is `{frames, locals, operands}`: plain vectors, copied for each checkpoint. Locals, parameters and
return values hold scalars and pointers. There's no heap.

### 4. References to locals: only as parameters, only down the stack

References are useful, for example for out-parameters and accumulators (`fn sum(MdbTreeNode *n, u64 &total)`).
They don't break the model as long as they're **second class**:
- **Only parameters can be references:** `fn f(u64 &acc)`. The argument must be a local or a reference parameter of
  the caller, and is passed by name, as in C++: `sum(n->mLeft, total);`.
- **A reference can't be stored in a local, returned or yielded.**

The guarantee follows from the shape of the stack. A reference can only be created by a caller, for a callee, and
the callee's frame is always above the caller's. The callee returns, and its frame (holding the reference) is
popped before the caller's frame can be. So a reference never outlives what it points to, and no lifetime checker
is needed. Swift's `inout` and Hylo work the same way.

The representation also survives checkpoints. Locals of all frames live in one `mLocals` vector, so a reference is
just an **index** into it, not a pointer (together with which stack it's on, always the main stack until decision
13). A copied `ProgramState` keeps valid references, which a pointer wouldn't.
This also works when a void function with a reference parameter pauses at a `yield`: the caller's frame is below it,
in the same checkpoint.

The cost is small:
- a new value kind ("reference to slot N") and two instructions, `LoadRef` and `StoreRef`;
- binder checks for the rules above;
- assigning to a reference parameter writes the caller's local. "Only locals can be written" still holds.

Not allowed: `&local` as a value (`auto p = &x;`). It would collide with `&` on target memory, which gives a target
pointer, and it would let references escape.

### 5. Why the operand stack is empty after a `yield`, and why the return type decides yielding

The operand stack holds the **temporaries of the expression being evaluated**. In `x = a + count(n);`, `a` is
pushed, then `count`'s argument; `Call` moves the argument into `count`'s locals, but `a` stays on the stack
while `count` runs. Then `count`'s result is pushed, `Add` combines them, and `StoreLocal` pops the result.

Every statement consumes what it pushed (`StoreLocal`, `JumpIfFalse`, `Yield`, `Pop` after a call statement), so
**between two statements, a frame has nothing on the operand stack**.

A `yield` is a statement, so the frame that yields is between statements. The frames below it are paused in calls.
If those calls are also statements (`walk(n->mLeft);`), with arguments that have already been moved into locals,
then every frame on the stack is between statements, and the whole operand stack is empty.

What breaks that is a call that can yield **inside an expression**, as in `x = 1 + walk(n);`. The `1` would be on
the stack during the pause. Copying it would still be *correct*: operands are values too, and `ProgramState`
keeps an operand vector anyway. So the empty stack is a simplification and something to assert, not a hard
requirement.

The real reasons for the rule are about meaning and simplicity:
- **Meaning:** what is the value of `1 + walk(n)` if `walk` yields 50 elements and then returns? Value-returning
  generators would need rules of their own. And a summary or a let can't yield at all.
- **No call-graph analysis:** without a marker in the syntax, finding out whether `f` can yield means checking
  whether its body yields, or calls something that yields, transitively. With recursion, that's a fixed-point
  iteration over the call graph, and the error messages are unclear ("`f` can't be used here because it calls `g`,
  which calls `h`, which yields"). With the rule, the signature says it: `-> T` never yields, no return type may
  yield.

### 6. Where functions live, and why shared functions are deferred

**Binding is per concrete type.** The view `MdbHashMap<.*>` is bound separately for each type it matches.
For `MdbHashMap<MdbHashEntry<int,int>>`, `using Entry = template_arg(0)` is `MdbHashEntry<int,int>`, and
`mTable.mCapacity` is "8 bytes at offset 16". For another instantiation, both can be different. Each produces its
own `BoundView`, with its own types, offsets and code.

**A function written inside a view** is bound together with the view, so it gets the same concrete types:

```
view MdbHashMap<.*> {
  using Entry = template_arg(0)
  fn keyAt(i64 slot) -> i64 { return ((Entry*)(mTable.mEntries + slot * mTable.mEntrySize))->mKey; }
}
```

Here `Entry` means whatever it means for the instantiation being bound. Such a function also sees `this` (its
members are the viewed object's), the view's `using`s and its `let`s, like the summary does. `this` is a hidden
first parameter of every view function (decision 12).

**"Can be called before they're written"** is a difference from lets, which can only use the lets before them.
Functions are bound in two passes, all the signatures first and then the bodies, so `walk` can call itself and
`a` can call `b`, which calls `a`.

**A function outside any view** (file level, shared by several views) has no matched type to resolve against:
- if it names concrete types (`fn depth(MdbTreeNode *n) -> u64`), the names must still be resolved against the debug
  info of a particular object file, so it's bound per object file, not once;
- if it should work for several types (`fn walk(T *n)` for any `T`), it's a template, which must be bound again for
  each type it's called with. That needs explicit type parameters, or types inferred from the call site, and a cache
  of instantiations.

Both are possible, but neither is needed for the containers we know about. Deferred.

### 7. Let values are cached per `Evaluator`, not in the paused state

After a resume they're evaluated again, which is correct because lets are pure and the target is stopped.

### 8. Calls go one way: JS → mscript

JS can call mscript and receive what it produces: copies of values (numbers, BigInts, strings), and later possibly
views into target memory, to be defined then. mscript can't call JS, and never calls into the target. This keeps
the paused state copyable, the step budget meaningful, and views read-only. The JS side itself is the optional
appendix.

### 9. Limits

- 100,000 steps for each summary part, `let` and `size`. The serializer runs on every display, and summaries can
  loop through functions;
- 1,000,000 steps per children request;
- a call depth of 1024 frames;
- `limit:` is an integer literal.

Frames are heap data, so the depth limit could be raised if 1024 turns out to be too low for degenerate trees. The
cost is bigger checkpoints.

### 10. Deferred

- locals holding text, records or fixed-size arrays;
- containers such as visited sets;
- file-level functions (decision 6);
- `template_arg(i, j)`;
- the frame environment: watch expressions, hover, logpoints and breakpoint conditions (decision 11);
- calls across views: nested summaries, and view functions as methods in expressions (`tree.count()`), decision 12;
- generators in range-`for` loops (decision 13);
- the features in "Gaps compared to other debuggers" at the end of this document, delegation among them.

### 11. Roots and binding environments

mscript is meant to be used outside of views later: watch expressions, hover, logpoints and breakpoint conditions.
Those scripts are bound against a **stack frame** instead of a type. Two choices in the implementation keep that from needing
a rework of the VM or the binder. Neither changes anything for views.

**Roots instead of `this`.** In a view, everything starts from one object: `mLength` is `this->mLength`, which
compiles to "push the viewed object; `Member +8`; `Load`". A frame script has no single object. `a.x + b->y` uses
two variables, and each lives somewhere else: `a` on the stack at `rbp - 24`, `b` perhaps in register `rdi`, with
bytes but no address.

The addresses can't be fixed when binding. The binding is reused (per function for a watch, per location for a
breakpoint), but where a variable is changes between evaluations: the frame base differs on every call, and DWARF
location lists can move a variable between a register and the stack. So the program refers to its objects **by
index**, and whoever runs it supplies them:

```
a.x + b->y   compiles to:

PushRoot 0 ; Member +0 ; Load                  ← a.x
PushRoot 1 ; Load ; Deref ; Member +4 ; Load   ← b->y
Add
```

Each root is `{type, address (optional), bytes}`. In a view there's exactly one, root 0, the viewed object. The
runtime passes its address as the hidden `this` parameter of the view function it runs (decision 12), so code inside
a view never uses `PushRoot`. A frame
caller would find each variable with mdb's existing location code, the same path that builds frame variables for
the variables panel, and pass it as a root. The VM never learns about DWARF, registers or frames, so it stays
testable with `FakeMemory`.

**Binding environments.** The binding environment answers one question, at **bind time**: *what do names mean in this
script?* The values those names have at run time are something else, the evaluation context: the actual frame,
registers and addresses.

| Environment | Names resolve to | When nothing matches | Roots | Can yield | Bound | Run |
|---|---|---|---|---|---|---|
| **View** | the view's `using`s, `let`s and functions | a member of `this` (`mLength` → `this->mLength`) | `[this]` | in `children` | once per matched type | on every display or page |
| **Frame** (watch, hover, logpoint) | locals of the innermost lexical block at the PC, then outer blocks, parameters and globals | a member of `*this` if the function has a `this` parameter, otherwise "no variable 'x' in this frame" | one per variable used | only in a `{ … yield … }` watch | per (expression, function) | on every stop |
| **Breakpoint condition** | the same as frame, at the breakpoint location's PC | the same as frame | one per variable used | no; the result must convert to `bool` | once per breakpoint location | on every hit |

So there are really two environments, **view** and **frame**. Watches, hover, logpoints and conditions are the frame
environment with different rules on top: whether yield is allowed, what the result must be, and when binding
happens.

In the binder, the scope stack stays as it is. The environment decides **what goes in the outermost scope,
and what happens when a name isn't found**. Only `ViewEnvironment` is implemented. A `FrameEnvironment` would
add a `FrameVariable` scope entry kind. Resolving a frame variable during binding gives it the next root index, and
the caller fills that root in at evaluation time. That's where the two choices meet.

The frame environment itself isn't implemented yet.

### 12. Calls across views: `this` is a parameter, and frames know their view

Some features need code bound for one type to call code bound for **another** type:
- **nested summaries:** a list's summary showing its first element's summary;
- **view functions as methods** in expressions: `tree.count()` in a watch, or `mList.count()` in another view. This is
  how mdb answers "call `size()`" without calling into the target: GDB's *xmethods* work the same way.

The calls themselves are deferred. Two choices in the implementation keep them possible without reworking the VM:

- **`this` is a hidden first parameter.** Every function of a view has `this` as local 0, a pointer to the viewed
  type: lets, summary parts, `size`, the children block, every `fn`. Implicit member access (`mLength`) compiles to
  `LoadLocal 0 ; Deref ; Member +8 ; Load`. Declared locals and parameters start at local 1.

  If functions read root 0 instead, a function of `MdbTree`'s view called from another view would see the other
  view's object as `this`.
- **A frame knows which view it runs.** `Frame` has `const BoundView *mView`, and `Call` looks up function `mA` in
  that view's `Program`. A later `CallView` instruction can push a frame for another view's function, with the other
  object's address as `this`. The pointer is safe in checkpoints: bound views only change in `Rebuild()`, which also
  forgets all cursors.

The runtime starts every view function with the viewed object's address as the `this` argument. That address can
be 0, for a value the program holds in registers. The dereference of the hidden `this` is therefore not
null-checked (`Deref` with `mA = 1`), and its reads are served from the root's bytes. Today, every
frame runs the same view, and every call passes the caller's `this` on.

Because `this` can differ between calls, **the let cache is keyed by (view, let, `this` address)**. Today it
always holds one view and one `this`, but the key is right from the start.

**Delegation doesn't need cross-view calls.** Every child is already a `sym::Value` that gets its own type's view,
so an array of `RefPtr<Foo>` shows each element through `RefPtr`'s view, and expanding it shows the `Foo` through
`Foo`'s view. That's one level more than you want: `[0]` → the `RefPtr` → the `Foo` → its members. Delegation
removes the level: a `RefPtr<Foo>` is displayed *as* its `Foo`, with `Foo`'s summary and children (Natvis
`ExpandedItem`). That's the runtime substituting a value before serializing it, for example with a view item
`display *mRawPtr`. No VM call between views is involved.

### 13. Generators in range-`for` loops (deferred)

A void function that yields could also be consumed by a loop, which can filter, transform or stop early:

```
fn walk(MdbTreeNode *n) yields int {
  if (!n) return;
  walk(n->mLeft);
  yield n->mValue;
  walk(n->mRight);
}

children {
  for (auto v : walk(mRoot))   // v is an int*: a pointer to each yielded object
    if (*v > 100) yield *v;
}
```

This isn't implemented. The design below shows that it can be added without changing the rest. The one thing the
implementation already does for it is represent references as `{stack, slot}`.

**The loop variable.** Locals hold scalars and pointers. A generator that yields lvalues (objects in memory) gives
the loop a **pointer** to each one, so `e->mKey` and `yield *e` read naturally. A generator that yields computed
scalars gives the loop the scalar.

**The element type is in the signature,** following decision 5 (the signature says it, no analysis of what calls
what):

```
fn walk(MdbTreeNode *n) yields int     // lvalues of type int in memory → the loop variable is an int*
fn bits() yields computed bool          // computed bools → the loop variable is a bool
```

Only generators used in a `for` need the annotation. The binder checks that every `yield` in the function matches
it, and that generators it calls as statements yield the same.

**Several stacks.** While a generator is suspended, its frames must stay alive. But the loop that consumes it keeps
running and makes calls of its own, so one stack can't hold both. Each generator started by a `for` gets **its own
stack**:

```cpp
struct Stack { std::vector<Frame> mFrames; std::vector<EvalValue> mLocals; std::vector<EvalValue> mOperands; };
struct ProgramState { std::vector<Stack> mStacks; /* 0 is the main stack */ bool mDone; };
```

The loop lowers to three new instructions:

```
<this> <args> ; Spawn walk ; StoreLocal h   ← a new stack, running walk; its handle in a local
top:  Next h → end                          ← resume h until it yields (push the element) or ends (jump to end)
      StoreLocal e ; <body> ; Jump top
end:  Drop h                                ← `break` jumps here too
```

A `yield` goes to whoever resumed the stack: the loop's `Next` for a spawned stack, the runtime for the main stack,
which is a pause exactly as before.

**What still holds:**
- **Checkpoints are copies.** The state is a vector of stacks of plain values.
- **The operand stack is empty when the program pauses** (decision 5). A pause only happens at a yield on the main
  stack. Every suspended stack is stopped at its own `yield`, and every waiting loop at its `Next`. All of those are
  statement boundaries.
- **Abandoning a generator is free.** `break` drops its stack: there are no destructors and no lifetimes.
- **Reference parameters stay safe.** The loop's frame outlives the generator it spawned, so a reference still only
  points at something that outlives it. That's why references are `{stack, slot}` already.
- **Limits.** The step budget counts every stack. The depth limit counts frames across all stacks, and a limit on
  the number of live stacks keeps recursive spawning bounded.

**Two ways to use a generator.** As a statement (`walk(n->mLeft);`), its yields go straight to the outer consumer:
one step per element. In a `for`, each element is handed to the loop. A recursive chain written as
`for (auto c : walk(n->mLeft)) yield *c;` passes each element up through every level, which is O(depth) per element,
like Python's `yield from`. The statement form stays the fast way to pass everything through.

**With decision 12,** a loop could also consume another view's children: `for (auto e : children(mTable)) …`.

## The language

### Values and types

- **Locals, parameters and return values** hold scalars (integers, `bool`, enums, floats) and pointers.
- `auto` takes the type of its initializer. `auto e = *entry;` on a struct is an error: "locals hold scalars and
  pointers, use a pointer".
- Expressions can still produce lvalues of any type (`*entry`, `n->mValue`). They just can't be stored in a local.
- **Locals are the only thing that can be written,** directly or through a reference parameter. Assigning to
  anything else is a bind error. A view must never change the program being debugged.

### Statements

- blocks `{ … }`;
- local declarations `type name = expr;` and `auto name = expr;`;
- `name = expr;`, `name += expr;`, `name -= expr;`, `++name;`, `name++;`, `--name;`, `name--;`;
- `if (cond) stmt [else stmt]`, `while (cond) stmt`, `for (init; cond; step) stmt`;
- `continue;` and `break;`;
- `return;` and `return expr;`;
- `yield value;` and `yield [key] value;`;
- a call as a statement: `walk(n->mLeft);`.

### `children`

```
children (size: expr, limit: N) { statements }   // a block: an anonymous void function
children (size: expr, limit: N) name(args)       // a call to a void function
children array(ptr, count)                       // contiguous elements
children none | default
```

The options are optional, and so are the parentheses when there are none.

- `size` is evaluated *without* running the block. It gives the client `indexedVariables`, so it can page.
- `limit` caps the number of elements. Reaching it ends the page with an error child that says so. `N` is an integer
  literal.
- **What `yield` takes.** Each child is a `sym::Value`, so the value needs a type from the debug info. It is one of:
  - **an lvalue** (`yield *entry;`, `yield n->mValue;`): an object in target memory. The child has an address, can be
    expanded, and is displayed with its own view;
  - **a computed scalar** (`yield (bool)(mWords[i / 64] >> (i % 64) & 1);`): a copy of a value the view
    produced. The child has no address, can't be expanded or written, and its type must exist as a DWARF base type
    (`bool`, `int`, `unsigned long`, …) in the object file.

  Use computed yields for elements that aren't addressable (bits in `std::vector<bool>`, packed or encoded fields)
  and for derived information (chain lengths, subtree sizes). The key can be anything `Format` can print.

### Functions

`fn` is a view item, like `let` and `summary`:

```
fn name(Type a, Type &b) -> ReturnType { statements }   // a value function
fn name(Type a, Type &b) { statements }                  // a void function
```

- **Value functions** (`-> T`) return a scalar or pointer and **can't yield**. They can be called anywhere an
  expression can go: in `summary`, `let`, `size`, conditions, and other functions.
- **Void functions** **can yield**. They can only be called as a statement, from a `children` block or another void
  function, or as the `children` item itself. This rule decides, without any analysis, whether a call can pause
  (decision 5).
- A function sees `this` (implicit member access), the view's `using`s and its `let`s, like the rest of the view.
  `this` is a hidden first parameter (decision 12).
- Functions can be called before they're written, and they can be recursive or call each other. Lets, by contrast,
  can only use the lets before them (decision 6).
- Parameters are passed by value, except **reference parameters** (`Type &name`), which refer to a local of the
  caller (decision 4). Their types follow the same rules as locals: no `auto`.
- A function can't have the name of a builtin (`array`, `text`, `sizeof`, `template_arg`, …).

### Limits

Every run of the VM has limits, so a broken view can't hang the debugger:

| Context | Can yield | Step budget | On exceeding it |
|---|---|---|---|
| each summary part, `let`, `size` | no | 100,000 instructions | the value shows `<view error: …: stopped after N steps>` |
| `children` block | yes | 1,000,000 instructions per request | the page ends with a `[view error]` child, "stopped after N steps" |

The call depth is limited to **1024 frames**: "call depth exceeded 1024", shown the same way. A cycle in a corrupt
tree then ends with an error instead of running out of memory.

## How it works, in one picture

```
children { for (...) { if (...) continue; yield x; } }      fn count(Node *n) -> u64 { ... }
        │ parser: Stmt tree, FunctionDef
        ▼
binder: types, offsets and casts. Expressions become BoundNode trees
        │ codegen: BoundNode trees and statements, flattened into one list of instructions
        ▼
                                             (local 0 is the hidden `this`)
   0: PushInt 0 ; StoreLocal 1               ← i64 slot = 0
   2: LoadLocal 1 ; LoadLocal 0 ; Deref ; Member … ; Load ; Less ; JumpIfFalse → 15
   9: …                ; JumpIfFalse → 12     ← `if (...) continue;` skips the yield
  11: <x> ; Yield
  12: LoadLocal 1 ; PushInt 1 ; Add ; StoreLocal 1   ← the for's step
  14: Jump 2
  15: Return
        │
        ▼
VM: Run(state, limits, onYield) executes instructions. The state is {frames, locals, operands}
runtime: saves states at page boundaries, so the next page continues from there
```

The binder is the place where all type checking happens. It produces typed `BoundNode` trees. Codegen only
flattens trees that are already checked. It never reports a type error.

## How it was built

The work was done in four parts, each split into steps that compile and are tested on their own:

| Part | What it added |
|---|---|
| A | The bytecode (`bytecode.h`), codegen from bound trees (`codegen.cpp`), and the VM (`evaluator.cpp`), replacing the tree-walking evaluator. Roots, and the hidden `this` parameter |
| B | Statements (`ast.h`, `parser.cpp`), the scope stack and `ViewEnvironment` (`binder.cpp`), lowering, `StoreLocal`/`Yield`/pausing, paging cursors (`registry.h`, `runtime.cpp`), escaped child names, keys, computed yields |
| C | The "Children blocks" section of `docs/views.md` |
| D | Functions: `fn`, lazy signatures, `Call`/`Fail`, the depth limit, reference parameters (`PushRef`/`LoadRef`/`StoreRef`), the "Functions" section of `docs/views.md` |

### Where the code is

- `src/views/lexer.*`, `parser.*`, `ast.h`: the language front end.
- `src/views/binder.cpp`: names, types, offsets, and lowering statements and functions. It's the only part of the
  compiler that depends on the debug information.
- `src/views/bound_ir.h`, `bytecode.h`, `codegen.*`: the typed trees, the instruction set, and the flattening.
- `src/views/evaluator.*`: the VM (`Start`, `Run`, `Call`, `Summary`, `Children`, `Size`).
- `src/views/registry.*`, `runtime.cpp`: matching views to types, serializing values, serving children pages, and
  the cursors that remember paused states.

### Tests

- `test/unittest/views_frontend.cpp`: the lexer and parser.
- `test/unittest/views_eval.cpp`: codegen, and the VM through hand-built programs (the binder isn't in the unit
  tests): pausing and resuming from a copy, recursion, the depth limit, references, computed yields.
- `test/driver/views.js`, against `test/containers.cpp`: `hashTableChildren`, `listChildren`, `runawayLoops`,
  `keyedChildren`, `computedChildren`, `treeChildren`, `functionErrors`, plus the earlier view tests. The view files
  are in `test/views/`.

### Deviations from the original plan

- **Renames.** The plan renamed the `BoundView` fields to function indices before the VM existed. They were renamed
  together with the VM, so that every step compiled.
- **The hidden `this` isn't null-checked,** because a value held in registers has address 0 (decision 12).
- **Budgets.** Each summary template part gets its own 100,000-step budget, not one budget for the whole summary. A
  budget error raised inside a `let` has no source location.
- **Declarations.** In statement position, `Foo *p = …` with a class type name counts as a declaration. The
  expression parser's type check alone doesn't recognize it.
- **No `memoryReference` for computed children.** It's decided by the memory object
  (`MemoryContentsObject::HasTargetAddress()`, false for `SynthesizedMemoryContentsObject`), not by the address
  value. Every serializer that writes `memoryReference` checks it.
- **Signatures are resolved lazily.** Function names are registered before any declaration is bound, and a
  signature's types are resolved the first time the function is called (or after all declarations). That's what
  lets a `let` call a function. A signature that uses a `using` declared after the `let` calling it gives
  "unknown type".
- **Binder tests are driver tests,** because the binder needs debug information: `functionErrors` and
  `treeChildren`.
- **The tree example's `sum` is a value function.** A value function can't call a void function, so `total()` calls
  a `sum` that returns its accumulator, and drops its result when called as a statement.
- **Test views.** The bitset view yields its bits without a key, so they're named `[0]`, `[1]`, …, and `setBits`
  yields `(u32)(i * 2)`, because `long` may not be in the test program's debug information.

### Known issues

- **A spurious limit error.** A block that yields exactly `limit` elements and would then finish still ends with
  "limit of N elements reached". The runtime stops at the limit without checking whether the program was about to
  end.
- **Untested paths.** Mutual recursion, `children walk(root)` without options, and several binder errors have no
  test: "can yield, it can only be called from…", "'f' must return a value", argument count and positional
  arguments, a reference type mismatch, "can't take the address of a local".
- **Call cost.** Each `Call` copies the callee's list of local types. It's cheap, but easy to avoid.

## Pitfalls

- **References into `mView.mTypes`** become invalid when types are added while binding (`FromDwarf`, `PointerTo`,
  `Bind`). Copy the fields you need, or fetch again with `Type(id)`. The same goes for `mProgram.mFunctions` while
  binding function bodies: hold an index, not a `Function &`, across anything that can add a function.
- **Short circuits are jumps.** If `&&`, `||` or `?:` emit both operands, then `node && node->mNext` reads through
  null.
- **The serializer runs on every display.** Anything that runs the block belongs in `ResolveChildren`. The summary
  can still loop through value functions, so it always runs with a budget.
- **Patch forward jumps.** It's easy to forget the `JumpIfFalse` of an `if` without an `else`, which goes straight
  to the end.
- **`continue` in a `for`** must jump to the step, not to the condition. Otherwise `for (i = 0; …; ++i) { if (…)
  continue; }` never ends. The budget would catch it, but it's a bug.
- **`Return` must drop the frame's locals.** Otherwise `mLocals` grows with every call, and checkpoints get bigger
  and bigger.
- **References are absolute slot indices,** plus the stack. A frame-relative index points at the wrong local as soon
  as it's passed through a second call.
- **Copy states, never share them.** A checkpoint is a copy. The run that serves a page must not modify a stored
  checkpoint.
- **The operand stack is empty after `Yield`.** The unit tests check it. If it isn't, a call that can yield was
  allowed in an expression: that's a binder bug.
- **Never recurse in C++ for mscript calls.** Recursion in mscript is a frame in `ProgramState`. Only lets use a
  nested `Call`, and their depth is bounded by the number of lets.
- **`this` is local 0** in every view function. Declared locals and parameters start at 1. Off-by-one slots read
  `this` as an integer, or the other way around.
- **Function indices.** The binder assumes a function's index in `mProgram.mFunctions` equals its position in the
  view's `fn` list. That holds because functions are registered before anything else is added to the program. Keep
  it that way, or add an explicit mapping.
- **Roots without an address.** A root can be bytes only (a variable in a register, decision 11). Reading it works.
  `&` on it, or yielding it as an lvalue, must be an error, not address 0.
- **Formatting.** The pre-commit hook rejects unformatted files: run `clang-format -i` (and `prettier --write` for
  `.js`) on what you touched.

---

## Appendix: calling mscript from QuickJS (optional)

**Not implemented. Only implement this when explicitly asked to.** Nothing in the rest of this document depends on
it.

Calls go one way (decision 8): JS calls mscript and receives what it produces, but mscript can't call JS. A JS call
runs with a budget of 1 000 000 steps per call, or per `next()` of an iterator. Exceeding it throws a JS exception.

### Step E1: the JS API

Add three methods to `JsVariable` (`src/mdbjs/variablejs.h`), next to `member()` and `members()`:

| Method | Returns |
|---|---|
| `v.viewSummary()` | the summary string, as displayed. Throws if the value has no view |
| `v.viewChildren()` | an iterator over the children, as `JsVariable`s named like in the variables panel |
| `v.viewCall(name, ...args)` | a value function: its result. A void function: an iterator over what it yields |

```js
for (const child of tree.viewChildren()) log(child.name(), child.toString())
const n = tree.viewCall("count", tree.mRoot)
const firstTen = [...take(tree.viewCall("walk", tree.mRoot), 10)]
```

**Arguments.** `this` is the variable's address: `v.viewCall(...)` runs the function with `v` as `this`. For an
integer parameter, a Number or a BigInt. For a pointer parameter, a BigInt address or a
`JsVariable` of pointer type (its value is used). Reference parameters can't be called from JS: there's no caller
local to refer to, so `viewCall` throws. Anything else throws.

**Results** are copies. Integers become a Number if they fit exactly, otherwise a BigInt. Pointers become a BigInt,
`bool` a boolean, floats a Number. Yielded elements become `JsVariable`s (values in target memory). Evaluation errors
throw, with the same message the variables panel would show.

**The iterator** is a new class in `src/mdbjs/` (`JsViewIterator`), holding:
- a `Ref<sym::Value>` for the viewed value;
- the function index and the `ProgramState`;
- the registry generation and the task's stop generation when it was created.

`next()` runs with `mMaxYields = 1`, and turns the element into a `JsVariable` with `MakeYieldedChild` (`runtime.cpp`). It
throws if:
- the views were reloaded since the iterator was created. A reload destroys the `BoundView`, so the iterator must not
  keep a pointer to it: look it up again with `ViewRegistry::Lookup` on each `next()`. Add a generation counter to
  `ViewRegistry`, incremented in `Rebuild()`;
- the target ran since then. States are only valid while the program is stopped, the same reason the cursors are
  forgotten (`ForgetCursors()`).

Each `next()` creates its own `Evaluator`. Lets are evaluated again, which is correct because the target hasn't
run.

**Checkpoint:** `viewsFromJs` in `test/driver/views.js`, through the evaluate request, the same way the tests load
view files:
- `viewSummary()` on `tree` is `size=7 sum=3500`;
- `viewCall("count", …)` on `tree` with its root is 7;
- spreading `viewChildren()` on `tree` gives 7 variables, `20` to `80`;
- `viewCall("walk", …)` on a subtree yields only that subtree;
- after a `next` (step), `next()` on an iterator created before throws.

### Step E2: document the JS API

In `docs/views.md`, add "Using views from scripts": the three methods, argument and result conversion, and when
iterators become invalid.

---

## Gaps compared to other debuggers

Features that other debuggers (Natvis in Visual Studio, LLDB formatters, GDB pretty printers, WinDbg's data model)
have, and mscript doesn't. Each can be added without changing the design. Calls across
views (nested summaries, view functions as methods) are covered by decision 12, and generators in loops by decision
13.

Calling functions in the target (`v.size()` running real code) is **not a gap**: it will not be supported. A view
must never change the program being debugged. View functions used as methods (decision 12) are the replacement.

| Feature | Who has it | How it would fit |
|---|---|---|
| **Delegation**: `RefPtr<Foo>` displayed *as* its `Foo`, with `Foo`'s summary and children, without the extra level | Natvis `ExpandedItem`, LLDB synthetic children | a runtime substitution before serializing, from a view item such as `display *mRawPtr` (decision 12). No calls between views |
| **A raw view**: the type's own members next to the view's children | Natvis `[Raw View]`, LLDB `--raw` | an extra child, added by the runtime only. Without it, a view hides the real struct, which hurts when the view is wrong |
| **Random-access children**: element `i` computed directly, without walking | Natvis `IndexListItems`, LLDB `get_child_at_index` | `children (size: n) index(i) expr`. Needed for `std::deque` and segmented arrays: jumping to the last page of a million-element deque would otherwise walk from the nearest checkpoint |
| **Tolerating missing members** across library versions | Natvis `Optional="true"` and `Condition` | a condition evaluated when binding, such as `if (has(mFoo))`, where the branch not taken isn't bound. Today one renamed member fails the whole view |
| **Building strings**: flag enums as `A \| C`, conditional summaries | Natvis (several `DisplayString`s with conditions), LLDB/GDB in Python | a summary block that yields text pieces: `summary { if (mFlags & 1) yield "A"; … }`. Same model as children blocks, and text values are copies |
| **Dynamic types**: an `nsISupports*` displayed as its concrete class | LLDB dynamic values, Natvis | look up the vtable to find the real type, then apply that type's view. Independent of mscript |
| **Globals in views**: an atom table, interned strings | Natvis | a global's address is fixed per object file, so it's a constant at bind time, not a root |
| **Named and indexed children together**: `[size]`, `[capacity]`, then the elements | Natvis `Item` and `ArrayItems`, DAP `namedVariables` / `indexedVariables` | a set of named children next to the paged indexed ones |
| **Alternate views**: a short and a detailed view per type, picked in a watch with a format specifier | Natvis `IncludeView` / `ExcludeView` | named views, and a format specifier that picks one |
| **Watch format specifiers**: `ptr,10` (a pointer shown as an array of 10), `,x` | Visual Studio, LLDB | part of the frame environment (decision 11). `FormatSpec` already exists |
| **Stack frame filters**: hide internal frames, show JS frames inside native stacks | GDB frame filters and decorators | a different kind of view, over frames instead of values. Needs text values and the frame environment |
| **Editor support**: syntax highlighting, completion of member names | — | a tree-sitter grammar for `.mdbv` first, a language server later |
