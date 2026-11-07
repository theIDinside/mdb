# Attach prototype

This driver checks whether one long-lived `mdb` can attach to the maintained `mdb_shim` before it execs `stackframes`,
hit a breakpoint, and reuse the parsed object file across two runs. The shim source is `src/mdb_shim.cpp` and its
executable is built beside `mdb` as `bin/mdb_shim`.

Build the test subjects with `cmake --build obj/debug -j16`, then run `node test/prototype/attach/run.js` from the
repository root. The script accepts an optional build directory as its first argument.
