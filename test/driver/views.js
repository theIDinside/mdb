/** @typedef { import("./client").DebugAdapterClient } DebugAdapterClient */

const { launchToGetFramesAndScopes, repoDirFile, readFileContents, getLineOf } = require('./client')
const { prettyJson } = require('./utils')

/**
 * Load a view file through the debug console, like a user would.
 * @param { DebugAdapterClient } debugAdapter
 */
async function loadViews(debugAdapter, relativePath) {
  const response = await debugAdapter.sendReqGetResponse('evaluate', {
    expression: `loadViews(${JSON.stringify(repoDirFile(relativePath))})`,
    context: 'repl',
  })
  await debugAdapter.assert(
    response.success && response.body.result.startsWith('loaded'),
    `Loaded views from ${relativePath}`,
    `Response: ${prettyJson(response)}`
  )
  // A view file with errors still loads, but reports the errors.
  await debugAdapter.assert(
    !response.body.result.includes('\n'),
    `No diagnostics for ${relativePath}`,
    `Result: ${response.body.result}`
  )
  return response.body.result
}

/**
 * @param { DebugAdapterClient } debugAdapter
 */
async function localsOf(debugAdapter, scopes) {
  const scope = scopes.find((s) => s.name == 'Locals')
  const response = await debugAdapter.sendReqGetResponse('variables', { variablesReference: scope.variablesReference })
  await debugAdapter.assert(response.success, 'Locals variables request succeeded', prettyJson(response))
  return Object.fromEntries(response.body.variables.map((v) => [v.name, v]))
}

/**
 * Continue to the next breakpoint and return the locals of the top frame.
 * @param { DebugAdapterClient } debugAdapter
 */
async function continueToLocals(debugAdapter, threadId) {
  await debugAdapter.contNextStop(threadId)
  const response = await debugAdapter.sendReqGetResponse('stackTrace', { threadId })
  const frameId = response.body.stackFrames[0].id
  const scopes = await debugAdapter.sendReqGetResponse('scopes', { frameId })
  return { frame: response.body.stackFrames[0], locals: await localsOf(debugAdapter, scopes.body.scopes) }
}

async function childrenOf(debugAdapter, variable, range = {}) {
  const response = await debugAdapter.sendReqGetResponse('variables', {
    variablesReference: variable.variablesReference,
    ...range,
  })
  await debugAdapter.assert(response.success, `Children of ${variable.name}`, prettyJson(response))
  return response.body.variables
}

async function expectValue(debugAdapter, locals, name, expected, { expandable } = {}) {
  console.log(`lookup ${name} in ${prettyJson(locals)}`)
  const variable = locals[name]
  await debugAdapter.assert(variable != undefined, `Found local '${name}'`, `Locals: ${prettyJson(locals)}`)
  await debugAdapter.assert(
    variable.value == expected,
    `${name} == ${expected}`,
    `Actual value: ${variable.value}. Variable: ${prettyJson(variable)}`
  )
  if (expandable !== undefined) {
    await debugAdapter.assert(
      (variable.variablesReference != 0) == expandable,
      `${name} is ${expandable ? '' : 'not '}expandable`,
      prettyJson(variable)
    )
  }
  return variable
}

/**
 * @param { DebugAdapterClient } debugAdapter
 */
async function stringSummaries(debugAdapter) {
  const { threads, scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/strings.cpp',
    ['LITERAL_STRINGS_BP', 'UNICODE_STRINGS_BP', 'STRING_PAIRS_BP', 'EMPTY_NULL_BP'],
    'literal_strings',
    'strings'
  )
  await loadViews(debugAdapter, 'test/views/strings.mdbv')

  let locals = await localsOf(debugAdapter, scopes)
  await expectValue(debugAdapter, locals, 'utf8_literal', '"Hello, World!"', { expandable: false })
  await expectValue(debugAdapter, locals, 'utf16_literal', '"Hello, World!"', { expandable: false })
  await expectValue(debugAdapter, locals, 'wide_path', '"/home/user/documents/report.pdf"')

  locals = (await continueToLocals(debugAdapter, threads[0].id)).locals
  await expectValue(debugAdapter, locals, 'emoji_utf8', '"Hello 😀 World"')
  await expectValue(debugAdapter, locals, 'emoji_utf16', '"Hello 😀 World"')
  await expectValue(debugAdapter, locals, 'cjk_utf8', '"日本語"')
  await expectValue(debugAdapter, locals, 'cjk_utf16', '"日本語"')
  await expectValue(debugAdapter, locals, 'latin_ext', '"éàü"')
  await expectValue(debugAdapter, locals, 'latin_ext_utf16', '"éàü"')

  locals = (await continueToLocals(debugAdapter, threads[0].id)).locals
  const greeting = await expectValue(debugAdapter, locals, 'greeting', '16 bytes / 16 code units', {
    expandable: true,
  })
  await expectValue(debugAdapter, locals, 'japanese', '15 bytes / 5 code units')
  // A view without children shows the type's members, which have views of their own.
  const members = Object.fromEntries((await childrenOf(debugAdapter, greeting)).map((v) => [v.name, v]))
  await expectValue(debugAdapter, members, 'utf8', '"Hello, Debugger!"')
  await expectValue(debugAdapter, members, 'utf16', '"Hello, Debugger!"')

  locals = (await continueToLocals(debugAdapter, threads[0].id)).locals
  // Null data pointers with a zero length are empty strings, not errors.
  await expectValue(debugAdapter, locals, 'empty_cstr', '""')
  await expectValue(debugAdapter, locals, 'empty_str', '""')
  await expectValue(debugAdapter, locals, 'empty_owned', '""')
  await expectValue(debugAdapter, locals, 'empty_owned_utf16', '""')
}

/**
 * @param { DebugAdapterClient } debugAdapter
 */
async function arrayChildren(debugAdapter, program = 'containers') {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    program
  )
  await loadViews(debugAdapter, 'test/views/containers.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  await expectValue(debugAdapter, locals, 'empty', 'length=0', { expandable: false })
  const ints = await expectValue(debugAdapter, locals, 'ints', 'length=1000', { expandable: true })
  await debugAdapter.assert(ints.indexedVariables == 1000, 'ints has 1000 indexed children', prettyJson(ints))

  // Paging: the client asks for a range of the children
  const page = await childrenOf(debugAdapter, ints, { start: 500, count: 10 })
  await debugAdapter.assert(page.length == 10, 'Got a page of 10 children', prettyJson(page))
  for (let i = 0; i < 10; ++i) {
    await debugAdapter.assert(
      page[i].name == `[${500 + i}]` && page[i].value == `${(500 + i) * 2}`,
      `Element ${500 + i}`,
      prettyJson(page[i])
    )
  }
  // Past the end there is nothing
  const pastEnd = await childrenOf(debugAdapter, ints, { start: 1000, count: 10 })
  await debugAdapter.assert(pastEnd.length == 0, 'No children past the end', prettyJson(pastEnd))

  // MdbAutoTArray<Point, 4> gets the view of its base class MdbTArray_Impl<Point>; it stores elements inline.
  const points = await expectValue(debugAdapter, locals, 'points', 'length=2', { expandable: true })
  const pointElements = await childrenOf(debugAdapter, points)
  await debugAdapter.assert(pointElements.length == 2, 'Two points', prettyJson(pointElements))
  const second = Object.fromEntries((await childrenOf(debugAdapter, pointElements[1])).map((v) => [v.name, v]))
  await expectValue(debugAdapter, second, 'x', '3')
  await expectValue(debugAdapter, second, 'y', '4')

  // Elements with views of their own
  const names = await expectValue(debugAdapter, locals, 'names', 'length=2')
  const nameElements = await childrenOf(debugAdapter, names)
  await debugAdapter.assert(
    nameElements[0].value == '"alpha"' && nameElements[1].value == '"beta"',
    'Elements are displayed by their own view',
    prettyJson(nameElements)
  )

  // Bitfields, and a view that applies through a base class
  await expectValue(debugAdapter, locals, 'flags', 'a=5 b=17 c=-3')
  await expectValue(debugAdapter, locals, 'derived', 'a=1 b=2 c=-1')

  // template_arg(1) is a value argument; typeof() names the type of an expression
  const buffer = await expectValue(debugAdapter, locals, 'buffer', '1 of 8, 8 bytes each', { expandable: true })
  const items = await childrenOf(debugAdapter, buffer)
  await debugAdapter.assert(items.length == 1 && items[0].name == '[0]', 'One item in the buffer', prettyJson(items))
  const item = Object.fromEntries((await childrenOf(debugAdapter, items[0])).map((v) => [v.name, v]))
  await expectValue(debugAdapter, item, 'x', '5')
  await expectValue(debugAdapter, item, 'y', '6')

  // Patterns match qualified names; inline namespaces are not part of them
  await expectValue(debugAdapter, locals, 'scoped', 'scoped=7 4 4 4')
  await expectValue(debugAdapter, locals, 'versioned', 'version=2')
}

/**
 * @param { DebugAdapterClient } debugAdapter
 */
async function viewErrors(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    'containers'
  )
  await loadViews(debugAdapter, 'test/views/containers.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  // A view that doesn't fit its type reports why, and the rest of the response is unaffected.
  const broken = locals['broken']
  await debugAdapter.assert(
    broken.value.startsWith('<view error:') && broken.value.includes("no member named 'mMissing'"),
    'Bind errors are displayed as the value',
    prettyJson(broken)
  )
  await debugAdapter.assert(locals['ints'].value == 'length=1000', 'Other views still work', prettyJson(locals))
  // ... and its members can still be inspected
  const members = await childrenOf(debugAdapter, broken)
  await debugAdapter.assert(members.length == 1 && members[0].name == 'mPresent', 'Raw members', prettyJson(members))

  const notATemplate = locals['notATemplate']
  await debugAdapter.assert(
    notATemplate.value.startsWith('<view error:') && notATemplate.value.includes('has no template arguments'),
    'template_arg() on a type without template arguments is a bind error',
    prettyJson(notATemplate)
  )

  // Children that can't be produced show an error child.
  const nullElements = await expectValue(debugAdapter, locals, 'nullElements', 'count=3')
  const errorChildren = await childrenOf(debugAdapter, nullElements)
  await debugAdapter.assert(
    errorChildren.length == 1 && errorChildren[0].value.includes('null pointer'),
    'Children error is reported',
    prettyJson(errorChildren)
  )

  // Reporting parse errors from the debug console
  const response = await debugAdapter.sendReqGetResponse('evaluate', {
    expression: `loadViews(${JSON.stringify(repoDirFile('test/views/does-not-exist.mdbv'))})`,
    context: 'repl',
  })
  await debugAdapter.assert(
    response.body.result.includes('could not open view file'),
    'Missing view files are reported',
    prettyJson(response)
  )
}

/**
 * View files can be given in the launch request, instead of loading them from the debug console.
 * @param { DebugAdapterClient } debugAdapter
 */
async function launchArgument(debugAdapter) {
  const stopped = debugAdapter.prepareWaitForEventN('stopped', 1, 5000, launchArgument)
  await debugAdapter.startLaunchSession(
    {
      program: debugAdapter.buildDirFile('containers'),
      stopOnEntry: true,
      type: 'ptrace',
      processId: debugAdapter.sessionId,
      views: [repoDirFile('test/views/containers.mdbv')],
    },
    () => {}
  )
  await stopped
  const line = getLineOf(readFileContents(repoDirFile('test/containers.cpp')), 'CONTAINERS_BP')
  await debugAdapter.setBreakpoints('test/containers.cpp', [line])
  const threads = await debugAdapter.threads()
  const { locals } = await continueToLocals(debugAdapter, threads[0].id)
  await expectValue(debugAdapter, locals, 'ints', 'length=1000')
  await expectValue(debugAdapter, locals, 'flags', 'a=5 b=17 c=-3')
}

/**
 * Built with -gsimple-template-names: type names in the debug info have no template arguments (`MdbTArray_Impl`),
 * so the names patterns match against are rebuilt from the template parameters.
 * @param { DebugAdapterClient } debugAdapter
 */
async function simpleTemplateNames(debugAdapter) {
  await arrayChildren(debugAdapter, 'containers_simple_template_names')
}

/**
 * Children from range(...): only the live slots of a hash table, in slot order, with paging over the live ones.
 * See MdbHashMap in test/containers.cpp for how keys end up in slots.
 * @param { DebugAdapterClient } debugAdapter
 */
async function hashTableChildren(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    'containers'
  )
  await loadViews(debugAdapter, 'test/views/hashtable.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  const membersOf = async (variable) =>
    Object.fromEntries((await childrenOf(debugAdapter, variable)).map((v) => [v.name, v]))

  // Capacity 8: 30 is in slot 0, 40 in 2, 10 in 4, 18 in 5 (it collided with 10), and slot 6 is a removed entry.
  const small = await expectValue(debugAdapter, locals, 'hashmap', 'count=4', { expandable: true })
  await debugAdapter.assert(small.indexedVariables == 4, 'hashmap has 4 indexed children', prettyJson(small))
  const entries = await childrenOf(debugAdapter, small)
  await debugAdapter.assert(
    entries.length == 4 && entries.every((entry, i) => entry.name == `${[30, 40, 10, 18][i]}`),
    'Children are named by their keys; free and removed slots are skipped',
    prettyJson(entries)
  )
  const expectedKeys = [30, 40, 10, 18]
  for (let i = 0; i < entries.length; ++i) {
    const members = await membersOf(entries[i])
    await expectValue(debugAdapter, members, 'mKey', `${expectedKeys[i]}`)
    await expectValue(debugAdapter, members, 'mData', `${expectedKeys[i] * 10}`)
  }

  // Keys 0..299, without the keys divisible by 10. Children are numbered by live entry, not by slot.
  const big = await expectValue(debugAdapter, locals, 'bigHashmap', 'count=270', { expandable: true })
  await debugAdapter.assert(big.indexedVariables == 270, 'bigHashmap has 270 indexed children', prettyJson(big))
  const liveKeys = [...Array(300).keys()].filter((key) => key % 10 != 0)
  const page = await childrenOf(debugAdapter, big, { start: 100, count: 10 })
  await debugAdapter.assert(page.length == 10, 'Got a page of 10 children', prettyJson(page))
  for (let i = 0; i < page.length; ++i) {
    await debugAdapter.assert(
      page[i].name == `${liveKeys[100 + i]}`,
      `Child ${100 + i} is named ${liveKeys[100 + i]}`,
      prettyJson(page[i])
    )
    const members = await membersOf(page[i])
    await expectValue(debugAdapter, members, 'mKey', `${liveKeys[100 + i]}`)
  }
  // The last page ends at the last live entry
  const last = await childrenOf(debugAdapter, big, { start: 265, count: 10 })
  await debugAdapter.assert(last.length == 5, 'The last page has the 5 remaining entries', prettyJson(last))
}

/**
 * Child names may contain quotes and must remain valid in the variables response.
 * @param { DebugAdapterClient } debugAdapter
 */
async function keyedChildren(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    'containers'
  )
  await loadViews(debugAdapter, 'test/views/keys.mdbv')
  const locals = await localsOf(debugAdapter, scopes)
  const names = await expectValue(debugAdapter, locals, 'names', 'length=2', { expandable: true })
  const children = await childrenOf(debugAdapter, names)
  await debugAdapter.assert(
    children.length == 2 && children[0].name == '"alpha"' && children[1].name == '"beta"',
    'Children use quoted text keys as names',
    prettyJson(children)
  )
}

/**
 * Computed scalar yields have no target address, while lvalue children keep their normal addresses.
 * @param { DebugAdapterClient } debugAdapter
 */
async function computedChildren(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    'containers'
  )
  await loadViews(debugAdapter, 'test/views/bitset.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  const bits = await expectValue(debugAdapter, locals, 'bits', 'count=70', { expandable: true })
  await debugAdapter.assert(bits.indexedVariables == 70, 'bits has 70 indexed children', prettyJson(bits))
  const values = await childrenOf(debugAdapter, bits)
  await debugAdapter.assert(values.length == 70, 'Got all 70 bits', prettyJson(values))
  for (let i = 0; i < values.length; ++i) {
    await debugAdapter.assert(
      values[i].name == `[${i}]` &&
        values[i].value == ([0, 3, 64, 69].includes(i) ? 'true' : 'false') &&
        values[i].variablesReference == 0 &&
        !Object.prototype.hasOwnProperty.call(values[i], 'memoryReference'),
      `Bit ${i} has the expected value and no target address`,
      prettyJson(values[i])
    )
  }
  const page = await childrenOf(debugAdapter, bits, { start: 60, count: 10 })
  await debugAdapter.assert(
    page.length == 10 &&
      page.every(
        (child, i) =>
          child.name == `[${60 + i}]` &&
          child.value == ([64, 69].includes(60 + i) ? 'true' : 'false') &&
          !Object.prototype.hasOwnProperty.call(child, 'memoryReference')
      ),
    'Page [60] to [69] contains the expected bits',
    prettyJson(page)
  )

  const setBits = locals['setBits']
  await debugAdapter.assert(
    setBits != undefined && setBits.variablesReference != 0,
    'setBits is expandable',
    prettyJson(setBits)
  )
  const setChildren = await childrenOf(debugAdapter, setBits)
  const expected = [
    ['0', '0'],
    ['3', '6'],
    ['64', '128'],
    ['69', '138'],
  ]
  await debugAdapter.assert(
    setChildren.length == expected.length &&
      setChildren.every(
        (child, i) =>
          child.name == expected[i][0] &&
          child.value == expected[i][1] &&
          !Object.prototype.hasOwnProperty.call(child, 'memoryReference') &&
          child.variablesReference == 0
      ),
    'setBits yields the four set-bit indices as computed children',
    prettyJson(setChildren)
  )
}

/**
 * Children from a block that follows pointers: a null-terminated list (with a size, so it can be paged) and a
 * circular list with a sentinel (without a size). See test/views/loops.mdbv.
 * @param { DebugAdapterClient } debugAdapter
 */
async function listChildren(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    'containers'
  )
  await loadViews(debugAdapter, 'test/views/loops.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  // 1, 2, 3, 4, 5
  const shortList = await expectValue(debugAdapter, locals, 'shortList', 'length=5', { expandable: true })
  await debugAdapter.assert(shortList.indexedVariables == 5, 'shortList has 5 indexed children', prettyJson(shortList))
  const values = await childrenOf(debugAdapter, shortList)
  await debugAdapter.assert(
    values.length == 5 && values.every((v, i) => v.name == `[${i}]` && v.value == `${i + 1}`),
    'shortList children are [0] = 1 ... [4] = 5',
    prettyJson(values)
  )

  // 0, 3, 6, ...: a page in the middle and the last page, which only has what's left
  const longList = await expectValue(debugAdapter, locals, 'longList', 'length=250')
  const page = await childrenOf(debugAdapter, longList, { start: 200, count: 10 })
  await debugAdapter.assert(
    page.length == 10 && page.every((v, i) => v.name == `[${200 + i}]` && v.value == `${(200 + i) * 3}`),
    'longList children [200] to [209]',
    prettyJson(page)
  )
  const last = await childrenOf(debugAdapter, longList, { start: 245, count: 10 })
  await debugAdapter.assert(last.length == 5, 'The last page of longList has 5 children', prettyJson(last))

  // Sentinel list: no size, so no indexedVariables; the walk stops back at the sentinel.
  const circularList = locals['circularList']
  await debugAdapter.assert(
    circularList.variablesReference != 0 && circularList.indexedVariables == undefined,
    'circularList is expandable, without indexedVariables',
    prettyJson(circularList)
  )
  const items = await childrenOf(debugAdapter, circularList)
  await debugAdapter.assert(items.length == 3, 'circularList has 3 items', prettyJson(items))
  for (let i = 0; i < items.length; ++i) {
    const members = Object.fromEntries((await childrenOf(debugAdapter, items[i])).map((v) => [v.name, v]))
    await expectValue(debugAdapter, members, 'mId', `${7 + i}`)
  }
}

/**
 * Blocks that would never end on their own: a list that loops back on itself (stopped by `limit: 10`), and an
 * endless `while (true) {}` (stopped by the step budget). Both end with an error child, and the debugger stays
 * responsive.
 * @param { DebugAdapterClient } debugAdapter
 */
async function runawayLoops(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    'containers'
  )
  await loadViews(debugAdapter, 'test/views/loops.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  const corrupt = await childrenOf(debugAdapter, locals['corruptList'])
  await debugAdapter.assert(corrupt.length == 11, '10 elements and an error child', prettyJson(corrupt))
  await debugAdapter.assert(
    corrupt.slice(0, 10).every((v, i) => v.value == `${100 + (i % 3)}`),
    'The elements cycle through 100, 101, 102',
    prettyJson(corrupt)
  )
  await debugAdapter.assert(
    corrupt[10].name == '[view error]' && corrupt[10].value.includes('limit'),
    'The last child says the limit was reached',
    prettyJson(corrupt[10])
  )

  const forever = await childrenOf(debugAdapter, locals['forever'])
  await debugAdapter.assert(
    forever.length == 1 && forever[0].name == '[view error]' && forever[0].value.includes('stopped after'),
    'An endless loop is stopped by the step budget',
    prettyJson(forever)
  )

  // The debugger is still responsive afterwards
  const again = await localsOf(debugAdapter, scopes)
  await debugAdapter.assert(again['shortList'] != undefined, 'Locals can still be requested', prettyJson(again))
}

/**
 * Recursive functions walk a tree in order, compute its size and sum, and preserve call state while paging.
 * @param { DebugAdapterClient } debugAdapter
 */
async function treeChildren(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    'containers'
  )
  await loadViews(debugAdapter, 'test/views/tree.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  const tree = await expectValue(debugAdapter, locals, 'tree', 'size=7 sum=3500', { expandable: true })
  await debugAdapter.assert(tree.indexedVariables == 7, 'tree has seven indexed children', prettyJson(tree))
  const treeChildren = await childrenOf(debugAdapter, tree)
  const treeKeys = [20, 30, 40, 50, 60, 70, 80]
  await debugAdapter.assert(treeChildren.length == treeKeys.length, 'tree has seven children', prettyJson(treeChildren))
  for (let i = 0; i < treeKeys.length; ++i) {
    await debugAdapter.assert(
      treeChildren[i].name == `${treeKeys[i]}` && treeChildren[i].value == `${treeKeys[i] * 10}`,
      `tree child ${treeKeys[i]} has value ${treeKeys[i] * 10}`,
      prettyJson(treeChildren[i])
    )
  }

  const bigTree = await expectValue(debugAdapter, locals, 'bigTree', 'size=300 sum=448500', { expandable: true })
  await debugAdapter.assert(bigTree.indexedVariables == 300, 'bigTree has 300 indexed children', prettyJson(bigTree))
  const page = await childrenOf(debugAdapter, bigTree, { start: 150, count: 10 })
  await debugAdapter.assert(page.length == 10, 'Got ten children from bigTree', prettyJson(page))
  for (let i = 0; i < page.length; ++i) {
    const key = 150 + i
    await debugAdapter.assert(
      page[i].name == `${key}` && page[i].value == `${key * 10}`,
      `bigTree child ${key}`,
      prettyJson(page[i])
    )
  }
  const last = await childrenOf(debugAdapter, bigTree, { start: 295, count: 10 })
  await debugAdapter.assert(last.length == 5, 'The last bigTree page has five children', prettyJson(last))

  const deepTree = locals['deepTree']
  await debugAdapter.assert(
    deepTree.value.includes('call depth exceeded 1024'),
    'deepTree summary reports the call depth limit',
    prettyJson(deepTree)
  )
  const deepChildren = await childrenOf(debugAdapter, deepTree, { start: 0, count: 2000 })
  await debugAdapter.assert(
    deepChildren.length > 0 &&
      deepChildren[deepChildren.length - 1].name == '[view error]' &&
      deepChildren[deepChildren.length - 1].value.includes('call depth exceeded'),
    'deepTree expansion ends with a call depth error child',
    prettyJson(deepChildren.slice(-2))
  )

  const again = await localsOf(debugAdapter, scopes)
  await debugAdapter.assert(again['tree'] != undefined, 'Locals still work after deepTree', prettyJson(again))
}

/**
 * Function binding and evaluation failures are shown on only the affected variables.
 * @param { DebugAdapterClient } debugAdapter
 */
async function functionErrors(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(
    debugAdapter,
    'test/containers.cpp',
    ['CONTAINERS_BP'],
    'main',
    'containers'
  )
  await loadViews(debugAdapter, 'test/views/functions.mdbv')
  const locals = await localsOf(debugAdapter, scopes)
  const errors = [
    ['scoped', "'text' is a builtin and can't be used as a function name"],
    ['versioned', "'walk' doesn't return a value, call it as a statement"],
    ['notATemplate', "'walk' doesn't return a value"],
    ['broken', "'n' is a reference parameter, pass a local"],
    ['forever', "function 'never' ended without returning a value"],
    ['corruptList', 'children needs a void function'],
  ]
  for (const [name, message] of errors) {
    const variable = locals[name]
    await debugAdapter.assert(
      variable != undefined && variable.value.startsWith('<view error:') && variable.value.includes(message),
      `${name} reports its function error`,
      prettyJson(variable)
    )
  }
}

/**
 * libstdc++'s std::vector<std::array<Point, 4>> test
 * @param { DebugAdapterClient } debugAdapter
 */
async function vectorOfArrayTest(debugAdapter, locals) {
  const arraysOfPoints = await expectValue(debugAdapter, locals, 'arraysOfPoints', 'size=3 capacity=3', {
    expandable: true,
  })
  await debugAdapter.assert(
    arraysOfPoints.indexedVariables == 3,
    'arraysOfPoints has 3 indexed children',
    prettyJson(arraysOfPoints)
  )

  const elements = await childrenOf(debugAdapter, arraysOfPoints, { start: 0, count: 3 })
  await debugAdapter.assert(elements.length == 3, 'Arrays of points has 3 arrays', prettyJson(elements))

  let multiplier = 1
  for (const array of elements) {
    const points = await childrenOf(debugAdapter, array)
    await debugAdapter.assert(points.length == 4, 'Array contains 4 points', prettyJson(points))
    let value = 1
    for (const p of points) {
      const members = await childrenOf(debugAdapter, p)
      const expX = value * multiplier
      const expY = (value + 1) * multiplier
      await debugAdapter.assert(members.length == 2, 'Point has 2 members', prettyJson(members))
      await debugAdapter.assert(members[0].value == expX, `Point.x=${expX}`, prettyJson(members))
      await debugAdapter.assert(members[1].value == expY, `Point.y=${expY}`, prettyJson(members))
      value += 2
    }
    multiplier = multiplier * 10
  }

  console.log(prettyJson(elements))
}

/**
 * libstdc++'s std::vector, and the packed std::vector<bool>. See test/views/stl.mdbv.
 * @param { DebugAdapterClient } debugAdapter
 */
async function stdVector(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(debugAdapter, 'test/stl.cpp', ['STL_BP'], 'main', 'stl')
  await loadViews(debugAdapter, 'test/views/stl.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  await expectValue(debugAdapter, locals, 'empty', 'size=0 capacity=0', { expandable: false })
  const ints = await expectValue(debugAdapter, locals, 'ints', 'size=1000 capacity=1024', { expandable: true })
  await debugAdapter.assert(ints.indexedVariables == 1000, 'ints has 1000 indexed children', prettyJson(ints))
  const page = await childrenOf(debugAdapter, ints, { start: 990, count: 20 })
  await debugAdapter.assert(page.length == 10, 'The last page stops at the size, not the capacity', prettyJson(page))
  for (let i = 0; i < page.length; ++i) {
    await debugAdapter.assert(
      page[i].name == `[${990 + i}]` && page[i].value == `${(990 + i) * 2}`,
      `Element ${990 + i}`,
      prettyJson(page[i])
    )
  }

  const points = await expectValue(debugAdapter, locals, 'points', 'size=2 capacity=2', { expandable: true })
  const pointElements = await childrenOf(debugAdapter, points)
  await debugAdapter.assert(pointElements.length == 2, 'Two points', prettyJson(pointElements))
  const second = Object.fromEntries((await childrenOf(debugAdapter, pointElements[1])).map((v) => [v.name, v]))
  await expectValue(debugAdapter, second, 'x', '3')
  await expectValue(debugAdapter, second, 'y', '4')

  // The elements are vectors too, and are displayed by the same view.
  const nested = await expectValue(debugAdapter, locals, 'nested', 'size=3 capacity=3', { expandable: true })
  const inner = await childrenOf(debugAdapter, nested)
  const innerValues = ['size=1 capacity=1', 'size=2 capacity=2', 'size=0 capacity=0']
  await debugAdapter.assert(
    inner.length == 3 && inner.every((v, i) => v.value == innerValues[i]),
    'Inner vectors are displayed by their own view',
    prettyJson(inner)
  )
  const innerElements = await childrenOf(debugAdapter, inner[1])
  await debugAdapter.assert(
    innerElements.length == 2 && innerElements[0].value == '2' && innerElements[1].value == '3',
    'Elements of an inner vector',
    prettyJson(innerElements)
  )

  await vectorOfArrayTest(debugAdapter, locals)

  // vector<bool> has a view of its own; its elements are bits, so they are computed and have no address.
  const bools = await expectValue(debugAdapter, locals, 'bools', 'size=70', { expandable: true })
  await debugAdapter.assert(bools.indexedVariables == 70, 'bools has 70 indexed children', prettyJson(bools))
  const bits = await childrenOf(debugAdapter, bools)

  await debugAdapter.assert(bits.length == 70, 'Got all 70 bits', prettyJson(bits))
  for (let i = 0; i < bits.length; ++i) {
    await debugAdapter.assert(
      bits[i].name == `[${i}]` &&
        bits[i].value == ([0, 3, 64, 69].includes(i) ? 'true' : 'false') &&
        !Object.prototype.hasOwnProperty.call(bits[i], 'memoryReference'),
      `Bit ${i}`,
      prettyJson(bits[i])
    )
  }
}

/**
 * libstdc++'s std::basic_string and std::basic_string_view. See test/views/stl.mdbv.
 * @param { DebugAdapterClient } debugAdapter
 */
async function stdString(debugAdapter) {
  const { scopes } = await launchToGetFramesAndScopes(debugAdapter, 'test/stl.cpp', ['STL_BP'], 'main', 'stl')
  await loadViews(debugAdapter, 'test/views/stl.mdbv')
  const locals = await localsOf(debugAdapter, scopes)

  await expectValue(debugAdapter, locals, 'emptyString', '""', { expandable: false })
  await expectValue(debugAdapter, locals, 'shortString', '"short"', { expandable: false })
  await expectValue(debugAdapter, locals, 'longString', '"a string that is too long for the inline buffer"')
  await expectValue(debugAdapter, locals, 'utf16String', '"日本語"')
  await expectValue(debugAdapter, locals, 'constString', '"const"', { expandable: false })

  // The elements of a vector of strings are displayed by the string view.
  const strings = await expectValue(debugAdapter, locals, 'strings', 'size=2 capacity=2', { expandable: true })
  const elements = await childrenOf(debugAdapter, strings)
  await debugAdapter.assert(
    elements.length == 2 && elements[0].value == '"one"' && elements[1].value == '"two"',
    'Strings in a vector',
    prettyJson(elements)
  )

  await expectValue(debugAdapter, locals, 'hello', '"string"', { expandable: false })
  await expectValue(debugAdapter, locals, 'emptyView', '""', { expandable: false })
}

const tests = {
  stdVector: () => stdVector,
  stdString: () => stdString,
  hashTableChildren: () => hashTableChildren,
  keyedChildren: () => keyedChildren,
  computedChildren: () => computedChildren,
  listChildren: () => listChildren,
  runawayLoops: () => runawayLoops,
  launchArgument: () => launchArgument,
  simpleTemplateNames: () => simpleTemplateNames,
  stringSummaries: () => stringSummaries,
  arrayChildren: () => arrayChildren,
  viewErrors: () => viewErrors,
  treeChildren: () => treeChildren,
  functionErrors: () => functionErrors,
}

module.exports = {
  tests: tests,
}
