#!/bin/bash
make -C midas/tests/subjects clean
make -C midas/tests/subjects all
cargo build
cargo build --release