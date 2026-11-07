/** GENERATED FILE - DO NOT ALTER */
#pragma once
#include <common/typedefs.h>

namespace mdb::logging::detail {

/**
 * FNV-1a hash function for compile-time format string hashing.
 * Must match the Python implementation in format_indexer.py.
 */
constexpr u32
HashFormatString(const char *str) noexcept
{
  u32 hash = 0x811c9dc5u; // FNV offset basis
  for (const char *p = str; *p != '\0'; ++p) {
    hash ^= static_cast<u32>(*p);
    hash *= 0x01000193u; // FNV prime
  }
  return hash;
}

/**
 * Compile-time lookup from format string hash to format ID.
 * Returns 0 if the format string is not found (unknown format).
 */
constexpr u32
GetFormatId(u32 hash) noexcept
{
  switch (hash) {
  case 0xa0569b74u:
    return 1; // "MDB CLI Arguments"
  case 0x5465b825u:
    return 2; // "{}"
  case 0x913e8658u:
    return 3; // "UI thread initialized and configured."
  case 0x5a752502u:
    return 4; // "Exited..."
  case 0xbb57c661u:
    return 5; // "[breakpoint loc]: Constructed breakpoint location at {}"
  case 0xdf587cc3u:
    return 6; // "[breakpoint loc]: Destroying breakpoint location at {}"
  case 0x618abbf6u:
    return 7; // "BreakpointLocation has no users."
  case 0x0959bd5bu:
    return 8; // "[{}:bkpt]: bp {} hit"
  case 0x7835b9f9u:
    return 9; // "Hit finish_bp_t {}"
  case 0xe30786d1u:
    return 10; // "Task {} hit code injection boundary breakpoint at {}"
  case 0x3c71d269u:
    return 11; // "Hit resume_bp_t {}"
  case 0x12a03de5u:
    return 12; // "[Core Event]: creating event ProcessExitEvent for {}:{}"
  case 0xcef16c59u:
    return 13; // "notify of new command... {} for {}"
  case 0xe24b731du:
    return 14; // "Exit code for thread {} exited={}"
  case 0xc22b4d37u:
    return 15; // "Cancel thread proceed action"
  case 0xdc0e65c1u:
    return 16; // "[InstructionStep] stepping 1 instruction for {}"
  case 0x559b07e4u:
    return 17; // "[inst step]: instruction step for {} ended"
  case 0x23d267e6u:
    return 18; // "[line step]: line step for {} ended"
  case 0xb12e7a27u:
    return 19; // "[line step]: continuing sub frame for {}"
  case 0x5917a7e3u:
    return 20; // "[line step]: no resume address set, keep istepping"
  case 0xebe72e93u:
    return 21; // "[line step]: Maybe set done to={}"
  case 0xdb5c026cu:
    return 22; // "Removing scheduler for {}"
  case 0xe9514eeau:
    return 23; // "Task has individual scheduler"
  case 0xf3a03e81u:
    return 24; // "remove scheduler for {}, stopped by user={}"
  case 0x641075f1u:
    return 25; // "creating task {}.{}"
  case 0x4fa85033u:
    return 26; // "Set signal={} to forward for task={}"
  case 0xa7634cd2u:
    return 27; // "clearing breakpoint location status"
  case 0x5e5fa8bcu:
    return 28; // "session {} is older than {}"
  case 0xf7efd294u:
    return 29; // "could not find process to erase: {}"
  case 0x014405beu:
    return 30; // "Debug Adapter serializers initialized."
  case 0x9db4ee66u:
    return 31; // "Adding supervisor to session with task leader tid {}"
  case 0x1874abccu:
    return 32; // "notifying {} messages"
  case 0x4e21b147u:
    return 33; // "partial unit supported not implemented, skipped {}"
  case 0x601a622au:
    return 34; // "DWARF Unit is a type unit: {}"
  case 0xc85f870au:
    return 35; // "[eh]: canonical frame address computed: 0x{:x}"
  case 0x26a8b6a5u:
    return 36; // "[unwinder] decoded {} CIE instructions"
  case 0x77b41a70u:
    return 37; // "[lnp]: computing lnp at {}"
  case 0xd995a88du:
    return 38; // "Could not find die reference"
  case 0xa5fba262u:
    return 39; // "Disassembling non-DWARF referenced instructions"
  case 0x4c06c180u:
    return 40; // "compute_expression of dwarf expression of {} bytes"
  case 0xd27f7a26u:
    return 41; // "object {} does not have .eh_frame, using null unwinder"
  case 0x514300dfu:
    return 42; // "{} clearing dies"
  case 0x0612d89eu:
    return 43; // "Computing line table for this {}"
  case 0xee3ebd28u:
    return 44; // "[WARNING]: Unexpected Tag in subprorogram die: {}"
  case 0x25483b23u:
    return 45; // "dies had different tags"
  case 0x19064ac0u:
    return 46; // "leftDie={}, rightDie={}"
  case 0x6c7be9edu:
    return 47; // "[warning]: No .symtab for {}"
  case 0xb0fe1f2du:
    return 48; // "Object file path not found={}"
  case 0x08404568u:
    return 49; // "Parsing objfile {}"
  case 0x8b597e81u:
    return 50; // ".debug_frame section found; parsing DWARF CFI section"
  case 0x9bc94031u:
    return 51; // "WARNING expected variable reference {} had no data associated with it."
  case 0xb4df7370u:
    return 52; // "Found resolver for type {}: {}, members={}"
  case 0x1961f15eu:
    return 53; // "regex searched {} in {}us"
  case 0x1aeacaf2u:
    return 54; // "declaration die 0x{:x} had no name attribute"
  case 0x9446d6dau:
    return 55; // "Destroying value {}"
  case 0x92fed68cu:
    return 56; // "[lazy transfer]: {} .. {}"
  case 0xccdd308eu:
    return 57; // "could not find function symbol for frame. Required to construct live variables."
  case 0xe4f2a420u:
    return 58; // "[eager read]: {}:+{}"
  case 0x00e36617u:
    return 59; // "Profiling logger shutting down with flush & close."
  case 0xbb7f7f90u:
    return 60; // "Writing {} profiling events"
  case 0xe447e130u:
    return 61; // "Awaiting join for profiler task"
  case 0xd3647c4bu:
    return 62; // "channels set: {}"
  case 0xe595c1a3u:
    return 63; // "[scopedfd]: Failed to open {}: {}"
  case 0x4a98d16fu:
    return 64; // "Successfully opened socket and connected to {}:{}"
  case 0x6a61c15fu:
    return 65; // "Failed to connect to {}:{}. Reported reason: {}"
  case 0xb60faff8u:
    return 66; // "TaskBase destructor"
  case 0x0f155f7cu:
    return 67; // "Could not find supervisor for session {} ({})"
  case 0xe5a7192au:
    return 68; // "user requested resume, all={}, thread={}"
  case 0x86268154u:
    return 69; // "task {} could not be resumed"
  case 0x2bd83e1eu:
    return 70; // "No task with id {} found"
  case 0x1719e840u:
    return 71; // "task {} is not stopped"
  case 0x9da56b17u:
    return 72; // "setBreakpoints with no target, postponed"
  case 0x51335924u:
    return 73; // "Setting post poned source breakpoints"
  case 0x267c351au:
    return 74; // "Source breakpoint requires a 'line' field"
  case 0xfcb1e585u:
    return 75; // "exception breakpoints not yet implemented"
  case 0xd44e8a2du:
    return 76; // "setInstructionBreakpoints with no target, postponed"
  case 0xeae0fe9fu:
    return 77; // "Setting post poned instruction breakpoints"
  case 0xc861b6f8u:
    return 78; // "Instruction breakpoints was set."
  case 0x29514077u:
    return 79; // "setFunctionBreakpoints with no target, postponed"
  case 0xfb505f79u:
    return 80; // "Setting post poned function breakpoints"
  case 0x3c36ce4au:
    return 81; // "Executing initialize request, session id={}"
  case 0x9f20ab16u:
    return 82; // "[launch]: Creating replay supervisor"
  case 0xd959cdcdu:
    return 83; // "RR Trace has been configured to be replayed!"
  case 0x78d2e49au:
    return 84; // "creating lauch request"
  case 0xf23d1a14u:
    return 85; // "Request missing fields. Request: {}"
  case 0xdef76671u:
    return 86; // "Base protocol request fields of incorrect type. Request: {}"
  case 0x72dcc447u:
    return 87; // "parsed request: {}"
  case 0x7dafd0ceu:
    return 88; // "[write]:[{}{}]"
  case 0xa9eab91cu:
    return 89; // "Invalid args for {} for command {}"
  case 0x9da1fa68u:
    return 90; // "Could not get or create objfile for {}"
  case 0x96a4ef0cu:
    return 91; // "Path to system interpreter: {}"
  case 0x1722588eu:
    return 92; // "copied {} symbol files"
  case 0x48b3cdc2u:
    return 93; // "{}: Setting rdebug version={}, address={}"
  case 0xa8339daau:
    return 94; // "[supervisor]: resume tracee {}"
  case 0xbd052009u:
    return 95; // "Thread {} stopped={}"
  case 0x680807e6u:
    return 96; // "Configuration done for supervisor {}"
  case 0x2f339bbdu:
    return 97; // "Emit stepped notification for {}"
  case 0xce535bacu:
    return 98; // "[all-stopped]: sending registered notifications"
  case 0x94dc12fau:
    return 99; // "No library info read during shared object event"
  case 0x82c7fd59u:
    return 100; // "stacktrace for {}"
  case 0x7d871049u:
    return 101; // "activation record cache not dirty"
  case 0x22bee49bu:
    return 102; // "No object file related to pc; abort call frame state building."
  case 0xb9715a96u:
    return 103; // "[stackframe]: WARNING, no frame info for pc {}"
  case 0x568ebf2cu:
    return 104; // "Processing EXEC for {}"
  case 0x73d65817u:
    return 105; // "Found base address in program header data in loaded binary: {}"
  case 0x917f233eu:
    return 106; // "Dynamic segment mapped in at {}"
  case 0x892e08f1u:
    return 107; // "[{}:bkpt:loc]: error while installing location: {}"
  case 0x90618afcu:
    return 108; // "Shutdown adapter client for {}"
  case 0x934a9424u:
    return 109; // "Debug state not consistent: no information about obj files read"
  case 0x9ab97d09u:
    return 110; // "Debug state is consistent; proceed to read dbg state."
  case 0xcede89a9u:
    return 111; // "Failed to read linkmap"
  case 0x2ef423a8u:
    return 112; // "Failed to read null-terminated string from tracee at {}"
  case 0x12278492u:
    return 113; // "Could not read rdebug_extended"
  case 0x1b7455ceu:
    return 114; // "thread {} is invalid"
  case 0xdfd7c562u:
    return 115; // "instructions at {} contains 0x{:x}"
  case 0x2d5f6746u:
    return 116; // "writing instructions at {} to 0x{:x}"
  case 0xff5851a6u:
    return 117; // "[{}.{}:bkpt]: enabling breakpoint at {}"
  case 0x99d8719bu:
    return 118; // "[{}.{}:bkpt]: disabling breakpoint at {}"
  case 0xb6d81a8bu:
    return 119; // "failed to read instructions at {}"
  case 0x61eb1c68u:
    return 120; // "failed to write instructions 0x{:x} at {}"
  case 0xf563a4d3u:
    return 121; // "SIGSTOP to {}.{}: {}"
  case 0x5b692f10u:
    return 122; // "[WARNING]: failed to resume {}: {}"
  case 0x26111c7fu:
    return 123; // "[{}.{}]: Did not resume, not recorded signal delivery stop."
  case 0xa3c4c177u:
    return 124; // "env={}"
  case 0x020bf430u:
    return 125; // "stop event is tracee-termination."
  case 0xc3c9a005u:
    return 126; // "Failed to resume task."
  case 0xb8a119d2u:
    return 127; // "[v|fork]: new process after fork {}"
  case 0x0eda13adu:
    return 128; // "Terminated by signal: {}"
  case 0xe5cbf696u:
    return 129; // "event was not vfork; disabling breakpoints in new address space."
  case 0x5832d797u:
    return 130; // "Event handler is deferring..."
  case 0x0775e8c9u:
    return 131; // "Reading auxv for {} at {}"
  case 0xbe51d201u:
    return 132; // "Reviving supervisor {}"
  case 0x91e8d6e5u:
    return 133; // "task {} exited..."
  case 0x53c59166u:
    return 134; // "Marking session {} as dead"
  case 0xc727ee1du:
    return 135; // "Reading from target failed, target is running"
  case 0x88281101u:
    return 136; // "[replay]: genesis time {}; compared to time {}"
  case 0xccc72522u:
    return 137; // "Read from target {} at 0x{:x}, {} bytes"
  case 0x853eb0b8u:
    return 138; // "Failed to read clone_args for clone system calls. That's bad."
  case 0xd43f160au:
    return 139; // "Started supervisor thread, initialized resources path to={}"
  case 0x6c619303u:
    return 140; // "Return supervisor handle: {:p}"
  case 0xe267f967u:
    return 141; // "Evaluating source code: '{}'"
  case 0x1c242a6eu:
    return 142; // "calling breakpoint function"
  case 0x33db626du:
    return 143; // "Conditional breakpoint fn failed: {}"
  case 0xb30f4928u:
    return 144; // "Invalid choice reported by evaluator."
  case 0x6a5fa92cu:
    return 145; // "Discarding parameter to log. It must be a string."
  case 0xd883df97u:
    return 146; // "Initializing JS Type {}"
  case 0xe1a5dcedu:
    return 147; // "exception in js engine: {}\\nStack trace:\\n{}"
  case 0x8a0df98fu:
    return 148; // "Failed to call resolver function '{}': {}"
  case 0x1725b8adu:
    return 149; // "Resolver function '{}' did not return an array"
  case 0x288748b6u:
    return 150; // "Element {} in resolver '{}' result is not a valid JsVariable"
  case 0x7ad271e5u:
    return 151; // "Adding resolver {} for type {}"
  case 0x3d2991dau:
    return 152; // "Register resolver {} with {}"
  default:
    return 0; // Unknown format
  }
}

} // namespace mdb::logging::detail
