#![allow(unused, unused_macros)]
use std::{panic, process::Command, sync::Once};

static BUILT_TEST_DEBUGGEES: Once = Once::new();

macro_rules! tests_dir {
    () => {
        concat!(env!("CARGO_MANIFEST_DIR"), "/tests/subjects")
    };
}

macro_rules! subjects {
    () => {
        concat!(tests_dir!(), "/executables")
    };
    ($e: expr) => {
        concat!(concat!(tests_dir!(), "/executables/"), $e)
    };
}

pub fn compile_subjects() {
    BUILT_TEST_DEBUGGEES.call_once(|| {
        let status = Command::new("make")
            .arg("all")
            .current_dir(tests_dir!())
            .spawn()
            .unwrap()
            .wait()
            .unwrap();
        assert!(status.success())
    });
}

fn run_test<T>(test: T) -> ()
where
    T: FnOnce() -> () + panic::UnwindSafe,
{
    compile_subjects();
    let result = panic::catch_unwind(|| test());
    assert!(result.is_ok())
}
// binary data taken from myfile1.c
const debug_info: &[u8] = &[
    // COMPILATION UNIT HEADER BEGIN
    0x21, 0x00, 0x00, 0x00, // unit_length, length of this entry, *excluding* the bytes of the initial_length field
    0x04, 0x00, // version
    0x00, 0x00, 0x00, 0x00, // debug_abbrev offset
    0x08, // pointer size
    // COMPILATION UNIT HEADER END

    // CONTRIBUTION 1 BEGIN
    0x01, 0x00, 0x00, 0x00, //
    0x00, 0x0C, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x02, 0x01, //
    0x06, 0x00, 0x00, 0x00, //
    0x00, 0x00,
    // CONTRIBUTION 1 END
];

const debug_abbrev: &[u8] = &[
    0x01, 0x11, // DW_TAG_COMPILE_UNIT
    0x01, 0x25,
    0x0E, 0x13, 0x0B, 0x03, 0x0E, 0x1B, 0x0E, 0x10, 0x17, 0x00, 0x00, 0x02, 0x24, 0x00, 0x0B, 0x0B, 0x3E, 0x0B, 0x03,
    0x0E, 0x00, 0x00, 0x00,
];

// 0x11, 0x25
#[test]
pub fn parse_debug_info() {
    let cu_header = midas::dwarf::compilation_unit::CompilationUnitHeader::from_bytes(debug_info);
    println!("Compilation Unit Header #1:\n {:?}", cu_header);
    let contribution_begins = cu_header.stride();
    assert_eq!(contribution_begins, 11);
}
