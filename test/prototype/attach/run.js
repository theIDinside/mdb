#!/usr/bin/env node
'use strict'

const { spawn } = require('node:child_process')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const { performance } = require('node:perf_hooks')

const timeoutMs = 20000
const repoRoot = path.resolve(__dirname, '../../..')
const buildDir = path.resolve(process.argv[2] || path.join(repoRoot, 'obj/debug'))
const logDir = fs.mkdtempSync(path.join(os.tmpdir(), 'mdb-attach-prototype-'))
const mdbPath = path.join(buildDir, 'bin/mdb')
const shimPath = path.join(buildDir, 'bin/mdb_shim')
const stackframesPath = path.join(buildDir, 'bin/stackframes')
const sourcePath = path.join(repoRoot, 'test/stackframes.cpp')

function findBreakpointLine() {
  const lines = fs.readFileSync(sourcePath, 'utf8').split(/\r?\n/)
  const index = lines.findIndex((line) => line.includes('// BPLine1'))
  if (index < 0) throw new Error(`Could not find // BPLine1 in ${sourcePath}`)
  return index + 1
}

class DapConnection {
  constructor(stream, input) {
    this.input = input
    this.buffer = Buffer.alloc(0)
    this.messages = []
    this.waiters = []
    stream.on('data', (chunk) => this.onData(chunk))
    stream.on('error', (error) => this.rejectWaiters(error))
    input.on('error', (error) => this.rejectWaiters(error))
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk])
    for (;;) {
      const headerStart = this.buffer.indexOf('Content-Length:')
      if (headerStart < 0) {
        const keep = Math.min(this.buffer.length, 'Content-Length:'.length - 1)
        this.buffer = this.buffer.subarray(this.buffer.length - keep)
        return
      }
      if (headerStart > 0) this.buffer = this.buffer.subarray(headerStart)

      const separator = this.buffer.indexOf('\r\n\r\n')
      if (separator < 0) return
      const header = this.buffer.subarray(0, separator).toString('ascii')
      const match = header.match(/Content-Length:\s*(\d+)/i)
      if (!match) {
        this.buffer = this.buffer.subarray(separator + 4)
        continue
      }

      const contentLength = Number(match[1])
      const bodyStart = separator + 4
      if (this.buffer.length < bodyStart + contentLength) return
      const body = this.buffer.subarray(bodyStart, bodyStart + contentLength).toString('utf8')
      this.buffer = this.buffer.subarray(bodyStart + contentLength)
      try {
        this.dispatch(JSON.parse(body))
      } catch (error) {
        this.rejectWaiters(new Error(`Invalid DAP message: ${error.message}; body=${body}`))
      }
    }
  }

  dispatch(message) {
    const index = this.waiters.findIndex((waiter) => waiter.predicate(message))
    if (index === -1) {
      this.messages.push(message)
      return
    }
    const [waiter] = this.waiters.splice(index, 1)
    clearTimeout(waiter.timer)
    waiter.resolve(message)
  }

  rejectWaiters(error) {
    for (const waiter of this.waiters.splice(0)) {
      clearTimeout(waiter.timer)
      waiter.reject(error)
    }
  }

  waitFor(predicate, description) {
    const index = this.messages.findIndex(predicate)
    if (index !== -1) return Promise.resolve(this.messages.splice(index, 1)[0])
    return new Promise((resolve, reject) => {
      const waiter = {
        predicate,
        resolve,
        reject,
        timer: setTimeout(() => {
          const waiterIndex = this.waiters.indexOf(waiter)
          if (waiterIndex !== -1) this.waiters.splice(waiterIndex, 1)
          reject(new Error(`Timed out after ${timeoutMs} ms waiting for ${description}`))
        }, timeoutMs),
      }
      this.waiters.push(waiter)
    })
  }

  send(request) {
    const body = Buffer.from(JSON.stringify(request), 'utf8')
    this.input.write(`Content-Length: ${body.length}\r\n\r\n`)
    this.input.write(body)
  }
}

function isResponse(message, requestSeq) {
  return message.type === 'response' && message.request_seq === requestSeq
}

async function request(connection, sequence, processId, command, args) {
  const requestSeq = sequence.value++
  connection.send({ seq: requestSeq, type: 'request', command, processId, arguments: args })
  const response = await connection.waitFor(
    (message) => isResponse(message, requestSeq),
    `response to ${command} request ${requestSeq}`
  )
  if (!response.success) {
    throw new Error(`${command} failed: ${response.message || JSON.stringify(response)}`)
  }
  return response
}

function waitForShimReady(shim) {
  return new Promise((resolve, reject) => {
    let pending = ''
    const timer = setTimeout(
      () => reject(new Error(`Timed out after ${timeoutMs} ms waiting for shim ready line`)),
      timeoutMs
    )
    shim.stderr.setEncoding('utf8')
    shim.stderr.on('data', (chunk) => {
      pending += chunk
      const lines = pending.split(/\r?\n/)
      pending = lines.pop()
      for (const line of lines) {
        const ready = line.match(/^ready (\d+)$/)
        if (ready) {
          clearTimeout(timer)
          resolve(Number(ready[1]))
          return
        }
        if (line) process.stderr.write(`shim: ${line}\n`)
      }
    })
    shim.once('error', (error) => {
      clearTimeout(timer)
      reject(error)
    })
    shim.once('exit', (code, signal) => {
      clearTimeout(timer)
      reject(new Error(`shim exited before ready (code=${code}, signal=${signal})`))
    })
  })
}

async function runOnce(connection, mdb, runNumber, line, sequence) {
  const shim = spawn(shimPath, ['--tracer-pid', String(mdb.pid), '--', stackframesPath], {
    stdio: ['pipe', 'ignore', 'pipe'],
  })
  let firstStopMs = null
  let goAt = null
  let success = false
  let failure = null
  let pid = null

  try {
    pid = await waitForShimReady(shim)
    const configToken = `attach-prototype-${runNumber}-${Date.now()}`
    const placeholderPid = 1000 + runNumber
    const sessionArgs = { adapterID: 'prototype', RRSession: false, configToken }
    await request(connection, sequence, placeholderPid, 'initialize', sessionArgs)
    await connection.waitFor(
      (message) => message.type === 'event' && message.event === 'initialized',
      `initialized event for run ${runNumber}`
    )

    await request(connection, sequence, placeholderPid, 'attach', {
      sessionType: 'ptrace',
      processId: pid,
      configToken,
    })
    await request(connection, sequence, pid, 'setBreakpoints', {
      source: { name: path.basename(sourcePath), path: sourcePath },
      breakpoints: [{ line }],
      configToken,
    })
    await request(connection, sequence, pid, 'configurationDone', { configToken })

    goAt = performance.now()
    shim.stdin.write('\n')
    const mainStop = await connection.waitFor(
      (message) => message.type === 'event' && message.event === 'stopped' && message.processId === pid,
      `first stopped event for process ${pid}`
    )
    firstStopMs = performance.now() - goAt
    if (!mainStop.body || !mainStop.body.threadId) throw new Error('first stopped event has no threadId')
    await request(connection, sequence, pid, 'continue', { threadId: mainStop.body.threadId })

    const breakpointStop = await connection.waitFor(
      (message) =>
        message.type === 'event' &&
        message.event === 'stopped' &&
        message.processId === pid &&
        message.body &&
        message.body.reason === 'breakpoint',
      `breakpoint stopped event for process ${pid}`
    )
    if (!breakpointStop.body.threadId) throw new Error('breakpoint stopped event has no threadId')
    await request(connection, sequence, pid, 'continue', { threadId: breakpointStop.body.threadId })
    await connection.waitFor(
      (message) => message.type === 'event' && message.event === 'exited' && message.processId === pid,
      `exited event for process ${pid}`
    )
    success = true
  } catch (error) {
    failure = error.message
  } finally {
    if (shim.stdin && !shim.stdin.destroyed) shim.stdin.end()
    if (shim.exitCode === null && shim.signalCode === null) shim.kill('SIGKILL')
  }

  return { runNumber, success, firstStopMs, failure, pid }
}

async function main() {
  let mdb
  let connection
  const results = []
  let parseCount = 0
  let aliveAfterRuns = false
  try {
    mdb = spawn(mdbPath, ['--keep-alive', '-l', logDir, '-t', '4'], { stdio: ['pipe', 'pipe', 'pipe'] })
    mdb.stderr.on('data', (chunk) => process.stderr.write(chunk))
    connection = new DapConnection(mdb.stdout, mdb.stdin)
    const sequence = { value: 1 }
    const line = findBreakpointLine()

    for (let runNumber = 1; runNumber <= 2; runNumber++) {
      results.push(await runOnce(connection, mdb, runNumber, line, sequence))
      if (runNumber === 1 && (mdb.exitCode !== null || mdb.signalCode !== null)) {
        results[0].success = false
        results[0].failure = results[0].failure || 'mdb exited between attach runs'
      }
    }

    aliveAfterRuns = mdb.exitCode === null && mdb.signalCode === null
    const coreLog = path.join(logDir, 'core.log')
    if (fs.existsSync(coreLog)) {
      parseCount = fs
        .readFileSync(coreLog, 'utf8')
        .split(/\r?\n/)
        .filter((lineText) => lineText.includes('Parsing objfile') && lineText.includes('stackframes')).length
    }
  } catch (error) {
    console.error(error.stack || error.message)
  } finally {
    if (mdb && mdb.exitCode === null && mdb.signalCode === null) mdb.kill('SIGKILL')
  }

  for (const result of results) {
    const time = result.firstStopMs === null ? 'n/a' : `${result.firstStopMs.toFixed(1)} ms`
    console.log(`Run ${result.runNumber}: ${result.success ? 'succeeded' : 'failed'}; time to first stop: ${time}`)
    if (result.failure) console.log(`  ${result.failure}`)
  }
  console.log(`stackframes parse count: ${parseCount}`)
  console.log(`mdb alive after both runs: ${aliveAfterRuns ? 'yes' : 'no'}`)
  console.log(`log directory: ${logDir}`)

  process.exitCode =
    results.length === 2 && results.every((result) => result.success) && parseCount === 1 && aliveAfterRuns ? 0 : 1
}

main().catch((error) => {
  console.error(error.stack || error.message)
  process.exitCode = 1
})
