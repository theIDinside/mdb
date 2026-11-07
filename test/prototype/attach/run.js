#!/usr/bin/env node
'use strict'

const { spawn } = require('node:child_process')
const fs = require('node:fs')
const net = require('node:net')
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
const shimMagic = Buffer.from([0x4d, 0x44, 0x42, 0x53, 0x48, 0x49, 0x4d, 0x00])
const shimHeaderSize = 16
const maxShimPayload = 1024 * 1024

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
    if (this.buffer.length + chunk.length > shimHeaderSize + maxShimPayload) {
      this.reject(new Error('shim hello exceeds the payload limit'))
      this.socket.destroy()
      return
    }
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

function parseShimHello(payload) {
  if (payload.length < 8) throw new Error('shim hello payload is truncated')
  const pid = payload.readUInt32BE(0)
  const argumentCount = payload.readUInt32BE(4)
  if (pid === 0 || argumentCount === 0 || argumentCount > 4096) throw new Error('shim hello has invalid pid/argc')
  let offset = 8
  const argv = []
  for (let index = 0; index < argumentCount; index++) {
    if (offset + 4 > payload.length) throw new Error('shim hello argument length is truncated')
    const length = payload.readUInt32BE(offset)
    offset += 4
    if (offset + length > payload.length) throw new Error('shim hello argument is truncated')
    argv.push(payload.subarray(offset, offset + length).toString('utf8'))
    offset += length
  }
  if (offset !== payload.length) throw new Error('shim hello has trailing data')
  return { pid, argv }
}

function shimFrame(type, payload = Buffer.alloc(0)) {
  const header = Buffer.alloc(shimHeaderSize)
  shimMagic.copy(header, 0)
  header.writeUInt16BE(1, 8)
  header[10] = type
  header.writeUInt32BE(payload.length, 12)
  return Buffer.concat([header, payload])
}

class ShimListener {
  constructor(socketPath) {
    this.socketPath = socketPath
    this.server = net.createServer((socket) => this.onConnection(socket))
    this.socket = null
    this.buffer = Buffer.alloc(0)
    this.hello = null
    this.waiter = null
    this.server.on('error', (error) => this.reject(error))
  }

  listen() {
    return new Promise((resolve, reject) => {
      this.server.once('error', reject)
      this.server.listen(this.socketPath, () => {
        this.server.removeListener('error', reject)
        resolve()
      })
    })
  }

  onConnection(socket) {
    if (this.socket) {
      socket.destroy()
      return
    }
    this.socket = socket
    socket.on('data', (chunk) => this.onData(chunk))
    socket.on('error', (error) => this.reject(error))
    socket.on('close', () => {
      if (!this.hello) this.reject(new Error('shim disconnected before hello'))
    })
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk])
    if (this.buffer.length < shimHeaderSize) return
    const header = this.buffer.subarray(0, shimHeaderSize)
    if (
      !header.subarray(0, 8).equals(shimMagic) ||
      header.readUInt16BE(8) !== 1 ||
      header[10] !== 1 ||
      header[11] !== 0
    ) {
      this.reject(new Error('invalid shim hello header'))
      this.socket.destroy()
      return
    }
    const payloadLength = header.readUInt32BE(12)
    if (payloadLength > maxShimPayload) {
      this.reject(new Error('shim hello exceeds the payload limit'))
      this.socket.destroy()
      return
    }
    if (this.buffer.length < shimHeaderSize + payloadLength) return
    if (this.buffer.length !== shimHeaderSize + payloadLength) {
      this.reject(new Error('shim sent unexpected data after hello'))
      this.socket.destroy()
      return
    }
    try {
      this.hello = parseShimHello(this.buffer.subarray(shimHeaderSize))
      if (this.waiter) this.waiter.resolve(this.hello)
    } catch (error) {
      this.reject(error)
      this.socket.destroy()
    }
  }

  reject(error) {
    if (this.waiter) this.waiter.reject(error)
  }

  waitForHello() {
    if (this.hello) return Promise.resolve(this.hello)
    return new Promise((resolve, reject) => {
      this.waiter = { resolve, reject }
      const timer = setTimeout(
        () => reject(new Error(`Timed out after ${timeoutMs} ms waiting for shim hello`)),
        timeoutMs
      )
      const finish = (callback) => (value) => {
        clearTimeout(timer)
        callback(value)
      }
      this.waiter.resolve = finish(resolve)
      this.waiter.reject = finish(reject)
    })
  }

  go() {
    if (!this.socket || this.socket.destroyed) throw new Error('shim socket is not connected')
    this.socket.write(shimFrame(2))
  }

  close() {
    if (this.socket && !this.socket.destroyed) this.socket.destroy()
    this.server.close()
    try {
      fs.unlinkSync(this.socketPath)
    } catch (error) {
      if (error.code !== 'ENOENT') throw error
    }
  }
}

async function runOnce(connection, mdb, runNumber, line, sequence) {
  const socketPath = path.join(logDir, `shim-${runNumber}.sock`)
  const listener = new ShimListener(socketPath)
  await listener.listen()
  const shim = spawn(shimPath, ['--tracer-pid', String(mdb.pid), '--socket', socketPath, '--', stackframesPath], {
    stdio: ['ignore', 'ignore', 'pipe'],
  })
  shim.stderr.setEncoding('utf8')
  shim.stderr.on('data', (chunk) => process.stderr.write(`shim: ${chunk}`))
  let firstStopMs = null
  let goAt = null
  let success = false
  let failure = null
  let pid = null

  try {
    const hello = await listener.waitForHello()
    pid = hello.pid
    if (hello.argv[0] !== stackframesPath) throw new Error(`shim requested unexpected target ${hello.argv[0]}`)
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
    listener.go()
    const breakpointStop = await connection.waitFor(
      (message) =>
        message.type === 'event' &&
        message.event === 'stopped' &&
        message.processId === pid &&
        message.body &&
        message.body.reason === 'breakpoint',
      `breakpoint stopped event for process ${pid}`
    )
    firstStopMs = performance.now() - goAt
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
    if (shim.exitCode === null && shim.signalCode === null) shim.kill('SIGKILL')
    listener.close()
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
