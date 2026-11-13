# Driver tests

These tests are built to behave much like what a real DA (debugger adapter) would look like. "Thankfully" I've decided to write the tests in Javascript (NodeJs) - now, some people might frown, cough or spasm once Javascript is mentioned, but I believe I got good arguments for why it's actually the perfect fit for tests like this.

1. `async` is 1st class
2. `json` is 1st class
3. Writing JS is easy - even if it's poorly written JS, getting the job done is what's most important.

## DWARF versions

With `BUILD_MDB_TESTS=ON`, each test subject is built under its usual name and
with a `_d5` suffix that forces DWARF 5 (for example, `dynamic_types_d5`). Shared
subject libraries get matching variants. The usual targets still follow
`USE_DWARF5`, which defaults to DWARF 4. Use `COMPILE_OPTIONS` in
`BuildTestSubject(...)` for flags that must apply to both variants.

Every driver test is registered twice in CTest; the DWARF 5 test has a `_d5`
suffix. Both registrations use the same test function and select the subject
through `debugAdapter.buildDirFile(...)`.

```sh
ctest --test-dir obj/debug --output-on-failure -R 'viewCalls'
ctest --test-dir obj/debug --output-on-failure -R 'viewCalls.*_d5$'
```

When invoking `test/driver/runDebugAdapterTest.js` directly, add `--dwarf=5` to
select the `_d5` subjects; omitting it selects the usual subjects.

## Test File design

Each file begins with `require(client)(__filename)` passing in the current test file name to the client code so that we remove a bunch of duplicated code that involves printing current file.

The test file should contain 1 async function that performs the test logic, which it then passes to `runTest(testFunction)` that executes it. Just look at `launch.js` test in [test files](../test/driver/launch.js) for an example of how to make a test. Then this test is added to the `CmakeLists.txt` file. This is done by adding it as a name to the cmake variable `DRIVER_TESTS` - but without it's file extension.

### Async

Due to async being 1st class, writing utilities for our "test harness" becomes as simple as:

```javascript
// member function taking `req`, the command as a string and `args` as a JS object of parameters for that command
sendReqGetResponse(req, args) {
  return new Promise((res) => {
    const serialized = serializeRequest(req, args);
    some_events_emitter.once(req, (response) => {
      res(response);
    });
    debuggerprocess.write(serialized);
  });
}
```

`serializeRequest` just takes the JS object and turns it into a string. Next, we've created a `EventEmitter` somewhere, called `some_events_emitter` to which we subscribe a one-time observer, with the same name as `req`. Inside this one-shot event handler, we resolve the `Promise` we've created, that resolves the `response` object - and then we fire off the request to the debugger.

The beauty of this is, we can have discreet "send and wait for response calls" like so:

```javascript
const threads = await sendReqGetResponse("threads", {});
```

Nifty!

### JSON

All this also works, because DAP is a protocol operating on JSON-objects/strings. Everything is a JSON-object (more or less) in Javascript.

```javascript
const launchArgs = {
  program: "/path/to/fooapp",
  programArgs: ["--some-param", "--print-help"],
};
/*
    launchArgs get serialized to a string, inside sendReqGetResponse, using: JSON.stringify(launchArgs)
  */
const launchResponse = await sendReqGetResponse("launch", launcArgs);
```

After seeing some of the test code in other debuggers this is truly a lovely experience in comparison! Not too shabby, JS, not too shabby at all!

Of course, we have to deserialize responses from our debugger somewhere, and so we do, just doing the simple `JSON.parse(string)`! It's this deserialized object that gets resolved in `sendReqGetResponse`.
