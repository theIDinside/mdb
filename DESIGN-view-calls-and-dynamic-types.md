# View calls and dynamic types (plan)

Temporary design document. Delete it once the work is specced and done. State as of 2026-11-08.

## Status

Implemented (uncommitted, branch `new-scripting-engine`): A1–A8, B1–B5, B7, and the first half of B6 (`{*p}` and
structs in summaries use the dynamic type). Not done: B6's variables panel (a value shown as its dynamic type).
Docs are updated: `docs/views.md`, `docs/mscript.md`, `docs/tutorials/mscript.md`.

Tested by `test/driver/viewCalls.js` against `test/dynamic_types.cpp` (`-fno-rtti`) and new unit tests in
`views_eval.cpp` and `views_frontend.cpp`. **Not tested:** a vtable in a shared library, construction vtables, and
real libxul (the Gecko views in `views/gecko.mdbv` only parse).

Where the implementation differs from the plan below:

- **Calls on the view's own type** (`view(mNext).sum()`) are `CallView`s to the code object being bound, not plain
  `Call`s, so they dispatch on the dynamic type too.
- **`view(x).summary` is verbatim text**: formatted without quotes, unlike `text()`. Joining it with quoted text gives
  quoted text.
- **Concatenation** has no limit of its own.
- **`nsSimpleURI` stores the whole spec** in `mSpec` in mozilla-central, so its view is like `nsStandardURL`'s. The
  example below that builds it from parts is wrong for Gecko, but that's what `test/views/viewcalls.mdbv` does for
  its `SimpleURI`.
- **nsCOMPtr:** the logpoint needs `.mRawPtr`: `view(aLoadState->mURI)` would look for `spec()` in the `nsCOMPtr`
  view.
- **Open questions** were answered with the defaults: dispatch is virtual by default, there's no soft form, a cycle
  is a bind error.
- `test/driver/views.js` now exports `loadViews` and its helpers. That also fixes
  `logpointsRequests.aggregatesWithViews`, which imported a `loadViews` that didn't exist.

## Goal

Let a logpoint (and any view) compute a value with another view's code, and let that code be picked by the
object's dynamic (most-derived) type. The driving example is `docshell/base/nsDocShell.cpp:9496`:

```
this={this} loadType={mLoadType,x} uri={view(aLoadState->mURI.mRawPtr).spec()}
```

`aLoadState->URI()->GetSpecOrDefault()` is a virtual call, which mdb won't make. Instead:

- `view(p)` finds the view for what `p` points to. If the object has a vtable, that's the view of its dynamic type:
  an `nsStandardURL`, `nsSimpleURI`, ….
- `.spec()` is a function defined in that view. It reads memory and returns text.

### What the views look like

`spec()` does what `GetSpec()` does in C++, but only by reading members. Each URI class gets its own version
(field names to be checked against mozilla-central when writing them):

```
// The interface. Every view of a class deriving from nsIURI can override spec(). This body is the default, used
// when the dynamic type has no view, or when it can't be determined (no .symtab, garbage pointer, …).
view nsIURI {
  fn spec() -> text { return "<unknown nsIURI>"; }
}

// http, https, file, …: the whole spec is stored in one string.
view mozilla::net::nsStandardURL {
  fn spec() -> text { return text(mSpec.mData, mSpec.mLength.mLength); }
  summary "{spec()}"
}

// about:, data:, javascript:, …: the spec is built from its parts, like nsSimpleURI::GetSpec does.
view nsSimpleURI {
  // Parameters can't be records, so the helper takes a pointer to the string. (Check that `nsCString`, a typedef,
  // resolves as a type name; otherwise write `nsTString<char>`.)
  fn str(nsCString *s) -> text { return text(s->mData, s->mLength.mLength); }

  fn spec() -> text {
    return str(&mScheme) + ":" + str(&mPath)
         + (mIsQueryValid ? "?" + str(&mQuery) : "")
         + (mIsRefValid ? "#" + str(&mRef) : "");
  }
  summary "{spec()}"
}

// Wraps a Rust MozURL, which is incomplete in the C++ debug info. No view: nsIURI's default spec() is used.
// view mozilla::net::DefaultURI { … }
```

When the logpoint is hit with an `https://` load:

1. `view(aLoadState->mURI.mRawPtr)`: the static type is `nsIURI`. When the logpoint is bound, this picks the
   `nsIURI` view and checks that it has a value function `spec()` returning text.
2. At run time, the vtable pointer of the object is in `vtable for mozilla::net::nsStandardURL`. Its view also
   defines `spec() -> text`, so that one runs, with `this` adjusted to the start of the `nsStandardURL`.
3. `spec()` reads `mSpec`, and the logpoint prints `uri=https://example.com/`.

Without Part B, step 2 doesn't happen: `nsIURI`'s `spec()` runs and prints `<unknown nsIURI>`. Writing the cast
yourself still works: `view((mozilla::net::nsStandardURL*)aLoadState->mURI.mRawPtr).spec()`.

The summaries make the same views useful elsewhere: an `nsStandardURL` in the variables panel, or as a hashtable
key, shows its URL.

There are two independent parts. **Part A** adds view calls; with static types only, it already works for one known
concrete class. **Part B** adds dynamic types in the symbol layer, and makes `view()` dispatch on them.

Out of scope: calling functions in the target (never), file-level shared functions (decision 6 in
`docs/tutorials/mscript.md`), `x->fn()` method sugar without `view(…)`, Rust trait objects.

## Where the code stands

`docs/tutorials/mscript.md` decision 12 already prepared for this:

- every view function has `this` as local 0, so another view's function can be called with a different `this`;
- `Frame` has `const CodeObject *mCodeObject` (`src/views/evaluator.h`), and `Op::Call` looks the function up in
  the frame's code object;
- the let cache is keyed by `(code object, let, this address)`;
- `ViewRegistry::Generation()` changes whenever bound views are thrown away.

What's missing:

- the evaluator's helpers resolve every `TypeId` through the evaluator's own `mCodeObject` (`Load`, `Convert`,
  `EvalBinary`, `Format`, `MakeInteger`, the `Negate`/`Convert` cases in `RunWithBudget`, …). TypeIds are per code
  object, so a frame running another code object would look up the wrong types;
- functions can only return numbers and pointers (`binder.cpp`, "return type of '…' must be a number or a pointer");
- there's no address-sorted index of ELF object symbols. `ObjectFile::mMinimalObjectSymbols` is a name map
  (`objfile.cpp`, `parseSymbols`);
- logpoints rebind on a new pc or object file (`Logpoint::Bind`, `src/bp.cpp`), not when views reload.

## Language

```
view(expr).name(args…)   // call value function `name` of the view for expr's type
view(expr).summary       // that view's summary, as text
```

- `view` is recognized in expressions only (like `sizeof`). The lexer only treats `view` as the start of a pattern
  at brace depth 0, and template expressions are lexed at depth 1, so `{view(x)…}` doesn't conflict.
- `expr` is a record lvalue or a pointer to a record. A pointer is dereferenced; null is a runtime error
  (`view() of a null pointer`).
- `view(expr)` isn't a value: it must be followed by `.name(…)` or `.summary`.
- The view is found when binding, from `expr`'s static type, through base classes as `ViewRegistry::Lookup` does
  today. No view, a view that failed to bind, an unknown function, a void function, or wrong arguments are bind
  errors. A logpoint reports them once and is marked unverified, as today.
- With Part B, the call is **virtual**: at run time, if the object has a dynamic type whose view also defines
  `name` with the same signature, that one runs. The static type's view is the interface, and its function is the
  default. The same applies to `.summary`.
- Functions can return `text` (`fn spec() -> text { … }`), and text can be concatenated with `+`. Text still can't be
  stored in locals.

See "What the views look like" above for the Gecko views this enables.

## Part A: view calls

### A1. Per-frame types in the evaluator (refactor, no behaviour change)

- Every helper that takes a `TypeId` also takes the `const CodeObject &` it belongs to, or reads it from the current
  frame. `Format` keeps using the evaluator's code object: everything it formats has been converted into the
  caller's types (A4).
- Done when `views_eval` and `test/driver/views.js` pass unchanged.

### A2. Text return values and text concatenation

- Parser: `text` as a return type.
- Binder: `-> text` is allowed for value functions. `return` must then be text. A call to such a function has the
  text type, and can be used where text can: template parts, `?:` with text on both sides, `summary`, `yield` keys.
  Assigning it to a local stays an error.
- `Text + Text` is text (new `BoundOp`/`Op::Concat`). The result is a new owned text; its length counts against the
  `limit:` of 4096 code units by default, and it is marked truncated if either side was.
- Evaluator: `Return` already moves the value to the caller's operand stack. Text handles are evaluator-local, and
  all frames run in one evaluator, so they stay valid. Text never reaches a checkpoint: locals can't hold it, and
  the operand stack is empty after a `yield` (decision 5).
- Update decision 3 ("return values hold scalars and pointers").
- Tests: `views_eval` (concat, truncation, return), `views.js` (a `-> text` function in a summary, the error for a
  local).

### A3. Parsing `view(…)`

- `ast.h`: `ExprKind::ViewCall` (`mA` = target, `mName` = function, arguments in `mArgs`) and
  `ExprKind::ViewSummary` (`mA` = target).
- `ParsePrimary`: identifier `view` followed by `(`, then `.`, then either `summary` or `name(args)`. Errors:
  "view(…) must be followed by .name(…) or .summary".
- Tests: `views_frontend.cpp`, including in frame templates and summary templates.

### A4. Binding

- **Lookup without a dependency on the registry singleton.** `Binder` gets a
  `std::function<const CodeObject *(sym::Type *)>` (null in unit tests). `ViewRegistry::FindAndBind` and
  `BindFrameTemplate` pass one that calls `ViewRegistry::Lookup`.
- **Calls into the view being bound** (`view(*mNext).count()` on a list node): if the target's type is the type
  being bound, emit a plain `Call` with the target's address as `this`. This is the common recursive case, and it
  avoids a lookup cycle. With Part B it must still dispatch dynamically if the type has a vtable, so it emits
  `CallView` then, with the callee resolved lazily (see below).
- **Cycles between different types** (view A calls B, which calls A): `ViewRegistry` tracks the types it is
  binding. `Lookup` of one of them fails, and the binder reports "view calls between 'A' and 'B' form a cycle". This
  is a v1 limitation. Lifting it needs the callee's signatures before its bodies are bound, across two binders.
- **Signature check.** The callee is fully bound when `Lookup` returns, so its signatures are resolved. Find `name`
  among its functions (add a name → index map to `Program`). It must be a value function. Check the argument count,
  then convert each argument to the parameter's type.
- **Types across code objects.** A `TypeId` only means something in its own `CodeObject`. The binder imports the
  callee's parameter and return types into the caller's table: through `mDwarf` when the type has one (`FromDwarf`),
  otherwise by rebuilding the builtin type, recursively for pointer elements. Never copy a `BoundType` as-is: its
  `mElement` refers to the callee's table.
- **Bound form:** `BoundOp::CallView` and `BoundOp::ViewSummary`. `mImm` indexes a new `CodeObject::mViewCallees`:

  ```cpp
  struct ViewCallee {
    const CodeObject *mCallee;   // static callee
    u32 mFunction;               // kNoFunction for .summary
    std::string mName;           // for dynamic dispatch (Part B)
    std::vector<TypeId> mCalleeParams;  // the callee's TypeIds, to retype arguments
  };
  ```

  The caller's code object depends on other bound views. It's only valid for one registry generation
  (`CodeObject::mGeneration`).

### A5. Running `CallView`

- `Op::CallView`, `mA` = callee index. Pop the arguments, retype each to the callee's parameter TypeId (the binder
  made them the same type), compute `this` = target address + `callee->mRootOffset` (the view may have matched a base
  class), and push a `Frame` for the callee's code object.
- `Frame` gets `TypeId mReturnAs`. `Return` retypes the value to it, so the caller sees its own TypeId. Plain calls
  leave it `kNoType`.
- The call shares the caller's budget and depth limit: it is a frame in the same `ProgramState` ("never recurse in
  C++ for mscript calls"). Lets in the callee are cached per `(callee, let, this)`, as now.
- Checkpoints may now hold frames of other code objects. That's safe: they are invalidated by the same generation
  bump.

### A6. `.summary`

- `Op::ViewSummary` produces text. The callee's summary is a template (one function per part) or an expression, so
  it can't be a single frame. It's evaluated by a C++ call that shares the step counter, like lets, and is bounded
  by a nesting limit of 8 (error: "summaries nested more than 8 deep"). Record this as a deviation from "never
  recurse in C++".
- A view without a summary gives the type name, as the variables panel does.
- With this, the logpoint's `AggregateFormatter` (`bp.cpp`) becomes the general rule: a record in a template part,
  in a view or a logpoint, is formatted with its view's summary through the same path, with the same nesting limit.
  That closes "nested summaries" in `docs/views.md`. Remove `SetAggregateFormatter`.

### A7. Logpoints and reloading

- `Logpoint` stores the registry generation it was bound in, and rebinds when it changed, in addition to the pc and
  object file checks.
- Tests (`test/driver/logpointsRequests.js`, `test/logpoints.cpp`): `view(p).fn()`, `view(x).summary`, a nested
  struct field formatted with its view, a missing view or function reported once, a reload of the view file changing
  the output.

### A8. Docs

`docs/views.md`, `docs/mscript.md` ("Not supported": remove nested summaries, calls between views); the tutorial's
decisions 3, 10 and 12; and the "Deviations" and "Tests" sections.

## Part B: dynamic types

This is a symbol-layer service. mscript only sees "the most-derived view". The implementation follows the Itanium
C++ ABI (Linux, macOS), as gdb's `set print object` and lldb's dynamic values do.

### B1. Vtable symbol index

- `ObjectFile` builds, lazily on first use so loading libxul doesn't get slower, an address-sorted vector of the
  `STT_OBJECT` symbols whose names start with `_ZTV`, from `.symtab` and `.dynsym`. Most Gecko vtables are hidden,
  so they're only in `.symtab`. Local builds have one, stripped release builds don't: then there are no dynamic
  types, and everything falls back to static types.
- `FindVtableSymbol(AddrPtr unrelocated) -> const MinSymbol *` returns the symbol whose `[address, address+size)`
  contains the address.

### B2. From a vtable symbol to a type

- Demangle with `__cxa_demangle` (already used in `common/panic.cpp`). `vtable for X` gives `X`.
- Normalize `X` to the form `QualifiedTypeName` produces: spacing in templates, `(anonymous namespace)`, and the
  names mdb rebuilds for `-gsimple-template-names`. This is the part most likely to need fixing against real libxul.
- Look the name up in that object file's name index. Move `Binder::LookupNamedType` to a shared
  `sym::FindTypeByQualifiedName(ObjectFile &, std::string_view)`. Prefer definitions over declarations: a vtable's
  class is usually only declared in most compilation units.
- Cache per object file: vtable symbol → `sym::Type *` (or "none").
- Check during implementation: recent clang may describe vtables in DWARF 5 (a variable with the class as its
  containing type). If mdb's DWARF reader can use that, it replaces the demangling and normalization. Keep the ELF
  path as the fallback.

### B3. `sym::ResolveDynamicType`

New `src/symbolication/dynamic_type.{h,cpp}`:

```cpp
struct DynamicType { sym::Type *mType; u64 mAddress; };  // the complete object
std::optional<DynamicType> ResolveDynamicType(tc::SupervisorState &, sym::Type *staticType, u64 address) noexcept;
```

1. `staticType` must be dynamic: it, or a base at offset 0, has a vtable pointer member (`_vptr$X` from clang,
   `_vptr.X` from gcc). Cache per type. In the Itanium ABI, a dynamic class has its vptr at offset 0.
2. Read the vptr at `address`.
3. Find the loaded object file whose mapping contains the vptr (it can be another library than the static type's),
   unrelocate it, then use B1. A `_ZTC` (construction vtable) or a missing symbol means "no dynamic type".
4. Read `offset_to_top` (an `i64` at `vptr - 16`). The complete object is at `address + offset_to_top`. This
   handles an `nsIURI *` that points into the middle of an object with multiple inheritance.
5. Get the type with B2.
6. Sanity check: `staticType` must be a base of the dynamic type, at offset `-offset_to_top`. Otherwise, for example
   with uninitialized memory, return `nullopt`.

`-fno-rtti` (which Gecko uses, I believe) makes no difference: this doesn't read `vptr[-1]`.

### B4. Runtime services for the evaluator

- A new interface next to `MemoryReader`:

  ```cpp
  class RuntimeServices {
  public:
    virtual const CodeObject *ViewFor(sym::Type *type) noexcept = 0;
    virtual std::optional<DynamicType> DynamicTypeOf(sym::Type *type, u64 address) noexcept = 0;
  };
  ```

  The `Evaluator` takes an optional one. The target implementation lives in `views/target_reader.h`, and the unit
  tests use a fake.
- A6's nested summaries use `ViewFor` at run time too, which replaces the logpoint's formatter lambda.

### B5. Virtual `view()` calls

- In `CallView` and `ViewSummary`: if services are present and the target's static type is dynamic, call
  `DynamicTypeOf`. If the dynamic type's view differs from the static callee and defines `name`, check that the
  signature matches the static callee's (same parameter count, same kind and size for each parameter and the
  return). Then run that function with `this` = complete object + its `mRootOffset`. Otherwise run the static callee.
- A signature mismatch is a runtime error that names both views.
- Cache `(callee index, dynamic CodeObject)` → function index in the evaluator, because a logpoint runs the same call
  on every hit.

### B6. Dynamic types for display (separate, can come later)

- `{*p}` in a template, and nested summaries, use the dynamic type's view (this falls out of B4 and B5).
- Variables panel: `SerializerFor` uses the dynamic type when the value has an address, and shows the value as that
  type (gdb's `print object on`), behind a setting that defaults to on. Children are those of the derived type. This
  touches `sym::Value` creation, so it gets its own spec.

### B7. Gecko views

`views/gecko.mdbv`: `nsIURI`, `nsStandardURL`, `nsSimpleURI` (and its subclasses through base-class matching).
`DefaultURI` wraps a Rust `MozURL`, which is incomplete in C++ debug info: give it the default `spec()` until mdb
can look up types across languages.

## Tests

- **A new subject**, `test/dynamic_types.cpp`, built with `-fno-rtti`:
  - an XPCOM-like hierarchy: `ISupports`, `IURI : ISupports`, and `StandardURL : IFileURL, ISerializable`, so that
    some interfaces are at a non-zero offset;
  - a class in an anonymous namespace, and a class template;
  - a class whose vtable is in a shared library (`dynamic_lib.cpp`);
  - an uninitialized or garbage pointer, for the sanity check;
  - a breakpoint inside a constructor, for construction vtables.
- **New view file** `test/views/viewcalls.mdbv`, and driver tests in `test/driver/views.js` and
  `test/driver/logpointsRequests.js`.
- **Unit tests**: `views_frontend` (parsing), `views_eval` (`CallView` with hand-built code objects, retyping, text
  return, concatenation, depth and budget shared across views, dynamic dispatch with a fake `RuntimeServices`).
- **Binder tests** are driver tests, as before.

## Order

A1 → A2 → A3 → A4 → A5 → A7 gives `view(x).fn()` with static types. It's useful on its own:
`view((mozilla::net::nsStandardURL*)p).spec()`. A6 can come next or in parallel. B1–B3 are independent of Part A
and can be developed at the same time against the variables panel's needs. B4–B5 join the two parts. B6 and B7 come
last.

## Open questions

1. Should `view()` be virtual by default (as planned), or should virtual dispatch be explicit (`view dynamic(x)`)?
   Default dispatch matches what users expect from a method call. Explicit is more predictable when the dynamic
   type's view is wrong.
2. Do we want a soft form now (`view?(x).fn()`, or `has_view(x)`), which gives a fallback instead of a bind error?
3. Is a cycle between two views' calls an acceptable v1 limitation?
4. B6: should dynamic types in the variables panel be on by default?
