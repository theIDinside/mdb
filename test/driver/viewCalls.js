/** @typedef { import("./client").DebugAdapterClient } DebugAdapterClient */

const {
  launchToGetFramesAndScopes,
  PrepareBreakpointArguments,
  readFileContents,
  repoDirFile,
  getLineOf,
} = require('./client')
const { prettyJson } = require('./utils')
const { loadViews, localsOf, childrenOf, expectValue } = require('./views')

const subject = 'test/dynamic_types.cpp'
const contents = readFileContents(repoDirFile(subject))
const line = (marker) => getLineOf(contents, marker)

async function launchToLocals(debugAdapter) {
  const { scopes, threads } = await launchToGetFramesAndScopes(
    debugAdapter,
    subject,
    ['DT_LOCALS'],
    'main',
    'dynamic_types'
  )
  await loadViews(debugAdapter, 'test/views/viewcalls.mdbv')
  return { locals: await localsOf(debugAdapter, scopes), threads }
}

async function expectIncludes(debugAdapter, locals, name, part) {
  const variable = locals[name]
  await debugAdapter.assert(
    variable != undefined && variable.value.includes(part),
    `${name} includes '${part}'`,
    `Variable: ${prettyJson(variable)}`
  )
}

/** @param { DebugAdapterClient } debugAdapter */
async function textFunctions(debugAdapter) {
  const { locals } = await launchToLocals(debugAdapter)
  await expectValue(debugAdapter, locals, 'standard', '"https://example.com/index.html"')
  await expectValue(debugAdapter, locals, 'simple', '"about:blank"')
  await expectValue(debugAdapter, locals, 'withQuery', '"data:text/plain?charset=utf-8"')
  // NestedURI has no view of its own; SimpleURI's applies through the base class.
  await expectValue(debugAdapter, locals, 'nested', '"view-source:https://example.com/"')
}

/** @param { DebugAdapterClient } debugAdapter */
async function staticViewCalls(debugAdapter) {
  const { locals } = await launchToLocals(debugAdapter)
  // Point's summary formats the members, a view() call with a pointer argument, and view().summary
  await expectValue(debugAdapter, locals, 'segment', '(1, 2) -> (4, 6) length=7 first=(1, 2)')
  // Recursion through view() on the view's own type
  await expectValue(debugAdapter, locals, 'first', 'sum=6')
  await expectValue(debugAdapter, locals, 'second', 'sum=5')
  await expectIncludes(debugAdapter, locals, 'cycleA', 'form a cycle')
}

/** @param { DebugAdapterClient } debugAdapter */
async function dynamicDispatch(debugAdapter) {
  const { locals } = await launchToLocals(debugAdapter)
  // view(mURI) is bound against IURI's view, and runs the spec() of the view of the dynamic type
  await expectValue(debugAdapter, locals, 'standardLoad', 'uri="https://example.com/index.html" type=1')
  await expectValue(debugAdapter, locals, 'simpleLoad', 'uri="about:blank" type=2')
  await expectValue(debugAdapter, locals, 'queryLoad', 'uri="data:text/plain?charset=utf-8" type=3')
  await expectValue(debugAdapter, locals, 'nestedLoad', 'uri="view-source:https://example.com/" type=4')
  // No view for OpaqueURI: IURI's own spec() runs
  await expectValue(debugAdapter, locals, 'opaqueLoad', 'uri="<unknown IURI>" type=5')
  await expectValue(debugAdapter, locals, 'hiddenLoad', 'uri="hidden:anonymous" type=6')
  await expectValue(debugAdapter, locals, 'templatedLoad', 'uri="templated:8080" type=7')
  await expectIncludes(debugAdapter, locals, 'nullLoad', 'view() of a null pointer')
  await expectValue(debugAdapter, locals, 'opaque', '"<unknown IURI>"')

  // The IURI inside a StandardURL is at a non-zero offset. IURI's summary calls view(this).spec(), which runs
  // StandardURL's.
  const children = await childrenOf(debugAdapter, locals['asInterface'])
  await debugAdapter.assert(
    children.length > 0 && children[0].value == '"https://example.com/index.html"',
    `*asInterface is shown with StandardURL's spec`,
    prettyJson(children)
  )
  // ISupports has no view. Showing the value as its dynamic type in the variables panel is not done yet.
  const supports = await childrenOf(debugAdapter, locals['asSupports'])
  await debugAdapter.assert(
    supports.length > 0 && supports[0].value == 'ISupports',
    `*asSupports is shown as its static type`,
    prettyJson(supports)
  )
}

async function setLogpoints(debugAdapter, specs) {
  const args = await PrepareBreakpointArguments(
    subject,
    specs.map((s) => s.marker)
  )
  args.breakpoints.forEach((bp, i) => {
    delete bp.condition
    if (specs[i].logMessage !== undefined) bp.logMessage = specs[i].logMessage
  })
  const response = await debugAdapter.setBreakpointsRequest(args)
  await debugAdapter.assert(response.success, 'Set breakpoints', prettyJson(response))
  return response
}

function collectConsole(debugAdapter) {
  const events = []
  debugAdapter.events.on('output', (body) => {
    if (body.category === 'console') events.push(body.output.replace(/\n$/, ''))
  })
  return events
}

async function runToMain(debugAdapter) {
  await debugAdapter.startRunToMain(debugAdapter.buildDirFile('dynamic_types'), 5000)
  const threads = await debugAdapter.threads()
  return threads[0].id
}

const message = 'type={mLoadType,x} uri={view(aLoadState->mURI).spec()} summary={*aLoadState->mURI}'

/** @param { DebugAdapterClient } debugAdapter */
async function logpointDynamicDispatch(debugAdapter) {
  const threadId = await runToMain(debugAdapter)
  await setLogpoints(debugAdapter, [{ marker: 'DT_LOAD', logMessage: message }, { marker: 'DT_END' }])
  const lines = collectConsole(debugAdapter)
  await loadViews(debugAdapter, 'test/views/viewcalls.mdbv')
  await debugAdapter.contNextStop(threadId, 5000)
  const expected = [
    'type=0x40001 uri="https://example.com/index.html" summary="https://example.com/index.html"',
    'type=0x40001 uri="about:blank" summary="about:blank"',
    'type=0x40001 uri="data:text/plain?charset=utf-8" summary="data:text/plain?charset=utf-8"',
    'type=0x40001 uri="view-source:https://example.com/" summary="view-source:https://example.com/"',
    'type=0x40001 uri="<unknown IURI>" summary="<unknown IURI>"',
    'type=0x40001 uri=<error: view() of a null pointer> summary=<error: null pointer dereference>',
  ]
  await debugAdapter.assert(
    JSON.stringify(lines) === JSON.stringify(expected),
    'Logpoint output uses the views of the dynamic types',
    `Expected: ${prettyJson(expected)}\nActual: ${prettyJson(lines)}`
  )
}

/** @param { DebugAdapterClient } debugAdapter */
async function logpointBindError(debugAdapter) {
  const threadId = await runToMain(debugAdapter)
  const response = await setLogpoints(debugAdapter, [
    { marker: 'DT_LOAD', logMessage: 'uri={view(aLoadState).spec()}' },
    { marker: 'DT_END' },
  ])
  const lines = collectConsole(debugAdapter)
  await loadViews(debugAdapter, 'test/views/viewcalls.mdbv')
  await debugAdapter.contNextStop(threadId, 5000)
  await debugAdapter.assert(
    lines.length === 1 && lines[0].includes("the view for 'LoadState' has no function 'spec'"),
    'A view() bind error is reported once',
    prettyJson(lines)
  )
}

/** @param { DebugAdapterClient } debugAdapter */
async function logpointRebindsAfterReload(debugAdapter) {
  const threadId = await runToMain(debugAdapter)
  await setLogpoints(debugAdapter, [
    { marker: 'DT_LOAD', logMessage: '{view(aLoadState->mURI).spec()}' },
    { marker: 'DT_SECOND' },
    { marker: 'DT_END' },
  ])
  const lines = collectConsole(debugAdapter)
  await loadViews(debugAdapter, 'test/views/viewcalls.mdbv')
  await debugAdapter.contNextStop(threadId, 5000)
  // The logpoint is bound; loading another view file throws away the views it calls into.
  await loadViews(debugAdapter, 'test/views/viewcalls_override.mdbv')
  await debugAdapter.contNextStop(threadId, 5000)
  const expected = [
    '"https://example.com/index.html"',
    '"overridden"',
    '"overridden"',
    '"overridden"',
    '"<unknown IURI>"',
    '<error: view() of a null pointer>',
  ]
  await debugAdapter.assert(
    JSON.stringify(lines) === JSON.stringify(expected),
    'The logpoint is bound again after the views change',
    `Expected: ${prettyJson(expected)}\nActual: ${prettyJson(lines)}`
  )
}

// The Gecko views use view() and text functions too; they must at least parse.
/** @param { DebugAdapterClient } debugAdapter */
async function geckoViewsParse(debugAdapter) {
  await runToMain(debugAdapter)
  await loadViews(debugAdapter, 'views/gecko.mdbv')
}

const tests = {
  geckoViewsParse: () => geckoViewsParse,
  textFunctions: () => textFunctions,
  staticViewCalls: () => staticViewCalls,
  dynamicDispatch: () => dynamicDispatch,
  logpointDynamicDispatch: () => logpointDynamicDispatch,
  logpointBindError: () => logpointBindError,
  logpointRebindsAfterReload: () => logpointRebindsAfterReload,
}

module.exports = { tests }
