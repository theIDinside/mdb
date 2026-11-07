const { PrepareBreakpointArguments, readFileContents, repoDirFile, getLineOf } = require('./client')
const { prettyJson } = require('./utils')
const { loadViews } = require('./views')

const subject = 'test/logpoints.cpp'
const source = repoDirFile(subject)
const contents = readFileContents(source)
const line = (marker) => getLineOf(contents, marker)

function collectConsole(da) {
  const events = []
  const listener = (body) => {
    if (body.category === 'console') events.push(body)
  }
  da.events.on('output', listener)
  return {
    events,
    lines: () => events.map((e) => e.output.replace(/\n$/, '')),
    stop: () => da.events.removeListener('output', listener),
  }
}
function collectStops(da) {
  const events = []
  const listener = (body) => events.push(body)
  da.events.on('stopped', listener)
  return { events, stop: () => da.events.removeListener('stopped', listener) }
}
async function breakpointsAt(file, specs) {
  const args = await PrepareBreakpointArguments(
    file,
    specs.map((s) => s.marker)
  )
  args.breakpoints.forEach((bp, i) => {
    // PrepareBreakpointArguments fills condition with null; SetBreakpoints expects
    // an omitted condition or a string, and currently crashes on explicit null.
    delete bp.condition
    if (specs[i].logMessage !== undefined) bp.logMessage = specs[i].logMessage
  })
  return args
}
async function launchWithBreakpoints(da, program, specs) {
  await da.startRunToMain(da.buildDirFile(program), 5000)
  const args = await breakpointsAt(subject, specs)

  const response = await da.setBreakpointsRequest(args)
  await da.assert(
    response.success &&
      response.body.breakpoints.length === specs.length &&
      response.body.breakpoints.every((b) => b.verified),
    `All ${specs.length} breakpoints verified`,
    prettyJson(response)
  )
  const threads = await da.threads()
  return { threadId: threads[0].id, response }
}
async function proceed(da, tid) {
  return da.contNextStop(tid, 5000)
}
async function stopAt(da, tid, expectedLine, context) {
  const trace = await da.stackTraceRequest({ threadId: tid })
  const frame = trace.body.stackFrames[0]
  await da.assert(
    frame.line === expectedLine,
    `Top frame is at line ${expectedLine}`,
    `Expected line ${expectedLine}, got ${frame.line}.\nFrame: ${prettyJson(frame)}\nCollected events: ${prettyJson(context)}`
  )
  return frame
}
async function assertCollected(da, condition, ok, expected, actual, ctx) {
  await da.assert(
    condition,
    ok,
    `Expected: ${prettyJson(expected)}\nActual: ${prettyJson(actual)}\nConsole output events: ${prettyJson(ctx.output.events)}\nStopped events: ${prettyJson(ctx.stopped.events)}`
  )
}
async function check(da, condition, ok, ctx) {
  await da.assert(
    condition,
    ok,
    `Console output lines: ${prettyJson(ctx.console)}\nStopped events: ${prettyJson(ctx.stops)}`
  )
}
async function setup(da, program, specs) {
  const running = await launchWithBreakpoints(da, program, specs)
  const output = collectConsole(da),
    stopped = collectStops(da)
  return { ...running, output, stopped }
}
async function logsEveryIteration(da) {
  const c = await setup(da, 'logpoints', [{ marker: 'LP_LOOP', logMessage: 'i={i} n={n}' }, { marker: 'LP_END' }])

  // Continue once; the plain breakpoint at LP_END bounds the output collection.
  const stopped = await proceed(da, c.threadId)
  const expectedLines = ['i=0 n=5', 'i=1 n=5', 'i=2 n=5', 'i=3 n=5', 'i=4 n=5', 'i=0 n=3', 'i=1 n=3', 'i=2 n=3']
  const actualLines = c.output.lines()
  const frame = await stopAt(da, c.threadId, line('LP_END'), {
    stoppedEvent: stopped,
    output: c.output.events,
    stoppedEvents: c.stopped.events,
  })

  await assertCollected(
    da,
    JSON.stringify(actualLines) === JSON.stringify(expectedLines) &&
      c.stopped.events.length === 1 &&
      stopped.reason === 'breakpoint' &&
      frame.line === line('LP_END'),
    'Eight ordered logpoint lines; the only stop is the plain breakpoint at LP_END',
    { lines: expectedLines, stoppedEvents: 1, stopLine: line('LP_END') },
    { lines: actualLines, stoppedEvents: c.stopped.events.length, stopLine: frame.line, reason: stopped.reason },
    c
  )
}
async function outputEventShape(da) {
  const c = await setup(da, 'logpoints', [{ marker: 'LP_LOOP', logMessage: 'loop hit' }, { marker: 'LP_END' }])
  await proceed(da, c.threadId)
  const e = c.output.events[0]
  await check(
    da,
    !!e &&
      e.category === 'console' &&
      e.output === 'loop hit\n' &&
      e.source?.path?.endsWith('test/logpoints.cpp') &&
      e.line === line('LP_LOOP'),
    'Output event has the DAP logpoint shape',
    { console: c.output.lines(), stops: c.stopped.events }
  )
}
async function parametersAndMembers(da) {
  const c = await setup(da, 'logpoints', [
    { marker: 'LP_PARAMS', logMessage: '{p->x} {s.y} {(*p).y} {name[1]} {initial} {cstr(name)}' },
    { marker: 'LP_END' },
  ])
  await proceed(da, c.threadId)
  await check(da, c.output.lines()[0] === "1 4 2 101 'e' 104 'h' \"hello\"", 'Parameters and members resolve', {
    console: c.output.lines(),
    stops: c.stopped.events,
  })
}
async function implicitThis(da) {
  const c = await setup(da, 'logpoints', [
    { marker: 'LP_METHOD', logMessage: '{mCount}|{this->mCount}|{by}|{cstr(mName)}' },
    { marker: 'LP_END' },
  ])
  await proceed(da, c.threadId)
  await check(
    da,
    JSON.stringify(c.output.lines()) === JSON.stringify(['0|0|1|"counter"', '1|1|2|"counter"']),
    'Implicit this resolves',
    { console: c.output.lines(), stops: c.stopped.events }
  )
}
async function shadowing(da) {
  const c = await setup(da, 'logpoints', [
    { marker: 'LP_SHADOW_INNER', logMessage: 'value={value}' },
    { marker: 'LP_SHADOW_OUTER', logMessage: 'value={value}' },
    { marker: 'LP_END' },
  ])
  await proceed(da, c.threadId)
  await check(da, JSON.stringify(c.output.lines()) === JSON.stringify(['value=2', 'value=1']), 'Innermost local wins', {
    console: c.output.lines(),
    stops: c.stopped.events,
  })
}
async function expressionsAndFormats(da) {
  const c = await setup(da, 'logpoints', [
    { marker: 'LP_LOOP', logMessage: '{i * 10 + 1} {i,x} {i == 2 ? 100 : -1} {(char)(i + 65)} {{literal}} tab:\t.' },
    { marker: 'LP_END' },
  ])
  await proceed(da, c.threadId)
  await check(
    da,
    JSON.stringify(c.output.lines().slice(0, 3)) ===
      JSON.stringify([
        "1 0x0 -1 65 'A' {literal} tab:\t.",
        "11 0x1 -1 66 'B' {literal} tab:\t.",
        "21 0x2 100 67 'C' {literal} tab:\t.",
      ]),
    'Expressions, formats, and escapes',
    { console: c.output.lines(), stops: c.stopped.events }
  )
}
async function aggregates(da, withViews = false) {
  const c = await setup(da, 'logpoints', [{ marker: 'LP_AGGREGATE', logMessage: '{pt} {v}' }, { marker: 'LP_END' }])
  if (withViews) await loadViews(da, 'test/views/stl.mdbv')
  await proceed(da, c.threadId)
  const s = c.output.lines()[0] || ''
  await check(
    da,
    withViews ? s === '{Point} size=3 capacity=3' : s === '{Point} {std::vector<int, std::allocator<int> >}',
    'Aggregate summary',
    { console: c.output.lines(), stops: c.stopped.events }
  )
}
async function multipleLogpointsAndBreakpoint(da) {
  const c = await setup(da, 'logpoints', [
    { marker: 'LP_LOOP', logMessage: 'loop' },
    { marker: 'LP_METHOD', logMessage: 'bump {by}' },
    { marker: 'LP_SHADOW_INNER' },
  ])
  await proceed(da, c.threadId)
  await stopAt(da, c.threadId, line('LP_SHADOW_INNER'), { console: c.output.lines(), stops: c.stopped.events })
  await check(
    da,
    JSON.stringify(c.output.lines()) === JSON.stringify(['loop', 'loop', 'loop', 'loop', 'loop', 'bump 1', 'bump 2']),
    'Logpoints and stopping bp preserve order',
    { console: c.output.lines(), stops: c.stopped.events }
  )
  await proceed(da, c.threadId)
  await check(
    da,
    JSON.stringify(c.output.lines().slice(7)) === JSON.stringify(['loop', 'loop', 'loop']),
    'Second loop logs three more times',
    {
      console: c.output.lines(),
      stops: c.stopped.events,
    }
  )
}
async function errorCase(da, message, marker = 'LP_LOOP') {
  const c = await setup(da, 'logpoints', [{ marker, logMessage: message }, { marker: 'LP_END' }])
  const bpId = c.response.body.breakpoints[0].id
  const changes = []
  const listener = (b) => changes.push(b)
  da.events.on('breakpoint', listener)
  await proceed(da, c.threadId)
  da.events.removeListener('breakpoint', listener)
  const diag = c.output.lines().filter((s) => s.startsWith('logpoint '))
  await check(
    da,
    diag.length === 1 &&
      c.output.lines().length === 1 &&
      changes.length === 1 &&
      changes[0].reason === 'changed' &&
      !changes[0].breakpoint.verified &&
      changes[0].breakpoint.id === bpId,
    'Bind/parse error reported once and breakpoint invalidated',
    { console: c.output.lines(), stops: c.stopped.events }
  )
}
async function unknownNameReportedOnce(da) {
  await errorCase(da, 'x={doesNotExist}')
}
async function parseError(da) {
  await errorCase(da, 'oops {i')
}
async function unknownEscape(da) {
  await errorCase(da, 'C:\\tmp\\q')
}
async function runtimeErrorIsLocal(da) {
  const c = await setup(da, 'logpoints', [
    { marker: 'LP_RUNTIME_ERROR', logMessage: 'a={null->x} b={ok}' },
    { marker: 'LP_END' },
  ])
  await proceed(da, c.threadId)
  await check(da, /^a=<error: .+> b=7$/.test(c.output.lines()[0] || ''), 'Runtime error is local to one part', {
    console: c.output.lines(),
    stops: c.stopped.events,
  })
}
async function breakpointEventIsValidJson(da) {
  await errorCase(da, '{nosuch"}')
}
async function registerVariable(da) {
  const c = await setup(da, 'logpoints_opt', [{ marker: 'LP_LOOP', logMessage: 'i={i}' }, { marker: 'LP_END' }])
  await proceed(da, c.threadId)
  const expected = [0, 1, 2, 3, 4, 0, 1, 2].map((i) => `i=${i}`)
  await check(
    da,
    JSON.stringify(c.output.lines()) === JSON.stringify(expected) &&
      !c.output.lines().some((s) => s.includes('<error:')),
    'Optimized variable location resolves',
    { console: c.output.lines(), stops: c.stopped.events }
  )
}
async function removeLogpoint(da) {
  const c = await setup(da, 'logpoints', [{ marker: 'LP_LOOP', logMessage: 'loop' }, { marker: 'LP_METHOD' }])
  await proceed(da, c.threadId)
  const args = await breakpointsAt(subject, [{ marker: 'LP_END' }])
  const res = await da.setBreakpointsRequest(args)
  await da.assert(
    res.body.breakpoints.length === 1 && res.body.breakpoints[0].verified,
    'Only end breakpoint remains',
    prettyJson(res)
  )
  await proceed(da, c.threadId)
  await check(da, c.output.lines().length === 5, 'Removed logpoint stays silent', {
    console: c.output.lines(),
    stops: c.stopped.events,
  })
}
async function replaceMessage(da) {
  const c = await setup(da, 'logpoints', [{ marker: 'LP_LOOP', logMessage: 'first' }, { marker: 'LP_METHOD' }])
  await proceed(da, c.threadId)
  const args = await breakpointsAt(subject, [{ marker: 'LP_LOOP', logMessage: 'second' }, { marker: 'LP_END' }])
  const res = await da.setBreakpointsRequest(args)
  await da.assert(
    res.body.breakpoints.every((b) => b.verified),
    'Replacement breakpoints verified',
    prettyJson(res)
  )
  await proceed(da, c.threadId)
  await check(
    da,
    JSON.stringify(c.output.lines()) ===
      JSON.stringify(['first', 'first', 'first', 'first', 'first', 'second', 'second', 'second']),
    'New message is used after replacement',
    { console: c.output.lines(), stops: c.stopped.events }
  )
}
async function logpointToBreakpoint(da) {
  const c = await setup(da, 'logpoints', [{ marker: 'LP_LOOP', logMessage: 'loop' }, { marker: 'LP_PARAMS' }])
  await proceed(da, c.threadId)
  const args = await breakpointsAt(subject, [{ marker: 'LP_LOOP' }, { marker: 'LP_END' }])
  const res = await da.setBreakpointsRequest(args)
  await da.assert(
    res.body.breakpoints.every((b) => b.verified),
    'Plain breakpoint replaces logpoint',
    prettyJson(res)
  )
  await proceed(da, c.threadId)
  await stopAt(da, c.threadId, line('LP_LOOP'), { console: c.output.lines(), stops: c.stopped.events })
  await check(da, c.output.lines().length === 5, 'Converted breakpoint stops without logging', {
    console: c.output.lines(),
    stops: c.stopped.events,
  })
}
async function pendingLogpointInSharedObject(da) {
  const file = 'test/dynamic_lib.cpp'
  await da.startRunToMain(da.buildDirFile('dynamicLoading'), 5000)
  const args = await PrepareBreakpointArguments(file, ['BPMI'])
  delete args.breakpoints[0].condition
  args.breakpoints[0].logMessage = 'miles={miles}'
  const res = await da.setBreakpointsRequest(args)
  await da.assert(
    res.body.breakpoints.length === 1 && res.body.breakpoints[0].verified === false,
    'Logpoint in the not-yet-loaded shared object is pending',
    `Expected one unverified breakpoint. Response: ${prettyJson(res)}`
  )
  const c = collectConsole(da)
  const threads = await da.threads()
  const { event_body: exited } = await da.sendReqWaitEvent(
    'continue',
    { threadId: threads[0].id, singleThread: false },
    'exited',
    10000
  )
  await da.assert(
    c.lines().includes('miles=1') && exited.exitCode === 0,
    'Pending shared-object logpoint resolves and logs its parameter',
    `Expected one 'miles=1' line and exit code 0. Console events: ${prettyJson(c.events)}; exit: ${prettyJson(exited)}`
  )
  c.stop()
}
async function programRunsToCompletion(da) {
  const c = await setup(da, 'logpoints', [{ marker: 'LP_LOOP', logMessage: 'hit' }])
  const result = await da.sendReqWaitEvent('continue', { threadId: c.threadId, singleThread: false }, 'exited', 10000)
  const expectedLines = ['hit', 'hit', 'hit', 'hit', 'hit', 'hit', 'hit', 'hit']
  const actualLines = c.output.lines()
  await assertCollected(
    da,
    JSON.stringify(actualLines) === JSON.stringify(expectedLines) && result.event_body.exitCode === 0,
    'All logpoint lines arrive before a clean exit',
    { lines: expectedLines, exitCode: 0 },
    { lines: actualLines, exitCode: result.event_body.exitCode },
    c
  )
}

function todoForLocationList(fn) {
  const error = new Error(`${fn.name} blocked by location list lookup: 'i' is not available at this location`)
  error.testName = fn.name
  throw error
}

const tests = {
  // Keep these assertions enabled in the functions above. The current product
  // bug fails to find a location list entry for locals at the logpoint PC.
  logsEveryIteration: () => logsEveryIteration,
  outputEventShape: () => outputEventShape,
  parametersAndMembers: () => parametersAndMembers,
  implicitThis: () => implicitThis,
  shadowing: () => shadowing,
  expressionsAndFormats: () => expressionsAndFormats,
  aggregates: () => aggregates,
  aggregatesWithViews: () => (da) => aggregates(da, true),
  multipleLogpointsAndBreakpoint: () => multipleLogpointsAndBreakpoint,
  unknownNameReportedOnce: () => unknownNameReportedOnce,
  parseError: () => parseError,
  unknownEscape: () => unknownEscape,
  runtimeErrorIsLocal: () => runtimeErrorIsLocal,
  breakpointEventIsValidJson: () => {
    throw new Error('BreakpointEvent message is not JSON-escaped')
  },
  registerVariable: () => todoForLocationList(registerVariable),
  removeLogpoint: () => removeLogpoint,
  replaceMessage: () => replaceMessage,
  logpointToBreakpoint: () => logpointToBreakpoint,
  pendingLogpointInSharedObject: () => pendingLogpointInSharedObject,
  programRunsToCompletion: () => programRunsToCompletion,
}
// Real expected-failure test retained for un-todoing once breakpoint messages are JSON escaped:
// async function breakpointEventIsValidJsonReal(da) { await errorCase(da,'{nosuch"}') }
module.exports = { tests }
