#![allow(unused, unused_macros)]
use std::{io::Read, panic, process::Command, sync::Once};

use midas::{
    bytereader,
    dwarf::{
        attributes::{self, AbbreviationsTableIterator},
        compilation_unit::CompilationUnitHeaderIterator,
        linenumber::{LineNumberProgram, LineNumberProgramHeaderVersion4},
    },
    leb128::decode_unsigned,
};

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
            .stdout(std::process::Stdio::null())
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
    unsafe {
        bytereader::ConsumeReader::set_dwarf32();
    }
    let result = panic::catch_unwind(|| test());
    assert!(result.is_ok())
}

/// Symbol name passed to this, must exactly match what we are looking for.
macro_rules! get_addr_from_objdump_symbolname_is_exact {
    ($path:expr, $symbol:expr) => {{
        let mut readelf_output = std::process::Command::new("objdump")
            .arg("-t")
            .arg(subjects!($path))
            .stdout(std::process::Stdio::piped())
            .spawn()
            .expect("failed to run readelf on binary");

        let grep = std::process::Command::new("grep")
            .arg("-w")
            .arg($symbol)
            .stdin(
                readelf_output
                    .stdout
                    .take()
                    .expect("failed to grab output from readelf"),
            )
            .output()
            .expect("failed to spawn grep");
        let result = String::from_utf8(grep.stdout).expect("failed to get string contents from grep");
        let parts: Vec<&str> = result.split(" ").collect();
        let part = parts.get(0);
        let remove_leading_zeroes = part
            .and_then(|s| s.chars().position(|c| c != '0'))
            .unwrap_or(0usize);
        let addr = part.and_then(|s| parse_hex_string(&s[remove_leading_zeroes..]).ok());
        addr
    }};
}

pub fn addr2line_compare(path: &str, symbol: &str, addr: usize) -> bool {
    let mut addr2line_output = std::process::Command::new("addr2line")
        .arg("-f")
        .arg(format!("{:X}", addr))
        .arg("-C")
        .arg("-s")
        .arg("-e")
        .arg(path)
        .output()
        .expect("failed to run readelf on binary");

    let result = String::from_utf8(addr2line_output.stdout).expect("failed to get string contents from grep");

    let lines: Vec<&str> = result.lines().collect();
    let line_1 = lines.get(0);
    if let Some(&s) = line_1 {
        s.contains(symbol)
    } else {
        false
    }
}

// parses a string from the LSB until EOF or until it encounters an "X" or "x"
pub fn parse_hex_string(s: &str) -> Result<usize, &str> {
    let mut value = 0;
    let mut multiplier = 1;
    let ident_found = false;
    for c in s.to_uppercase().chars().rev() {
        value += match c {
            '0' => 0 * multiplier,
            '1' => 1 * multiplier,
            '2' => 2 * multiplier,
            '3' => 3 * multiplier,
            '4' => 4 * multiplier,
            '5' => 5 * multiplier,
            '6' => 6 * multiplier,
            '7' => 7 * multiplier,
            '8' => 8 * multiplier,
            '9' => 9 * multiplier,
            'A' => 10 * multiplier,
            'B' => 11 * multiplier,
            'C' => 12 * multiplier,
            'D' => 13 * multiplier,
            'E' => 14 * multiplier,
            'F' => 15 * multiplier,
            'X' => return Ok(value),
            _ => return Err("hex parse failed"),
        };
        multiplier *= 16;
    }
    Ok(value)
}

/// binary data taken from myfile1.c
/// .debug_info
const DEBUG_INFO: &[u8] = &[
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

/// .debug_abbrev
const DEBUG_ABBREV: &[u8] = &[
    0x01, 0x11, // DW_TAG_COMPILE_UNIT
    0x01, 0x25, 0x0E, 0x13, 0x0B, 0x03, 0x0E, 0x1B, 0x0E, 0x10, 0x17, 0x00, 0x00, 0x02, 0x24, 0x00, 0x0B, 0x0B, 0x3E,
    0x0B, 0x03, 0x0E, 0x00, 0x00, 0x00,
];

/// .debug_str
const DEBUG_STR: &[u8] = &[
    0x47, 0x4E, 0x55, 0x20, 0x43, 0x31, 0x37, 0x20, 0x31, 0x30, 0x2E, 0x33, 0x2E, 0x30, 0x20, 0x2D, 0x6D, 0x74, 0x75,
    0x6E, 0x65, 0x3D, 0x67, 0x65, 0x6E, 0x65, 0x72, 0x69, 0x63, 0x20, 0x2D, 0x6D, 0x61, 0x72, 0x63, 0x68, 0x3D, 0x78,
    0x38, 0x36, 0x2D, 0x36, 0x34, 0x20, 0x2D, 0x67, 0x20, 0x2D, 0x66, 0x61, 0x73, 0x79, 0x6E, 0x63, 0x68, 0x72, 0x6F,
    0x6E, 0x6F, 0x75, 0x73, 0x2D, 0x75, 0x6E, 0x77, 0x69, 0x6E, 0x64, 0x2D, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x73, 0x20,
    0x2D, 0x66, 0x73, 0x74, 0x61, 0x63, 0x6B, 0x2D, 0x70, 0x72, 0x6F, 0x74, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x2D, 0x73,
    0x74, 0x72, 0x6F, 0x6E, 0x67, 0x20, 0x2D, 0x66, 0x73, 0x74, 0x61, 0x63, 0x6B, 0x2D, 0x63, 0x6C, 0x61, 0x73, 0x68,
    0x2D, 0x70, 0x72, 0x6F, 0x74, 0x65, 0x63, 0x74, 0x69, 0x6F, 0x6E, 0x20, 0x2D, 0x66, 0x63, 0x66, 0x2D, 0x70, 0x72,
    0x6F, 0x74, 0x65, 0x63, 0x74, 0x69, 0x6F, 0x6E, 0x00, 0x64, 0x77, 0x61, 0x72, 0x66, 0x5F, 0x73, 0x74, 0x61, 0x6E,
    0x64, 0x61, 0x72, 0x64, 0x5F, 0x65, 0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x73, 0x2F, 0x64, 0x31, 0x2F, 0x6D, 0x79,
    0x66, 0x69, 0x6C, 0x65, 0x31, 0x2E, 0x63, 0x00, 0x2F, 0x68, 0x6F, 0x6D, 0x65, 0x2F, 0x63, 0x78, 0x2F, 0x64, 0x65,
    0x76, 0x2F, 0x6F, 0x70, 0x65, 0x6E, 0x73, 0x6F, 0x75, 0x72, 0x63, 0x65, 0x2F, 0x6D, 0x64, 0x65, 0x62, 0x75, 0x67,
    0x2F, 0x6D, 0x69, 0x64, 0x61, 0x73, 0x2F, 0x74, 0x65, 0x73, 0x74, 0x73, 0x2F, 0x73, 0x75, 0x62, 0x6A, 0x65, 0x63,
    0x74, 0x73, 0x00, 0x63, 0x68, 0x61, 0x72, 0x00,
];

#[test]
pub fn parse_dwarf() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of myfile1.o");
        let dbg_info = elf
            .get_dwarf_section(midas::dwarf::Section::DebugInfo)
            .expect("Failed to get .debug_info");
        let debug_abbrev = elf
            .get_dwarf_section(midas::dwarf::Section::DebugAbbrev)
            .expect("Failed to get .debug_info");
        let debug_str = elf
            .get_dwarf_section(midas::dwarf::Section::DebugStr)
            .expect("Failed to get .debug_info");
    })
}

#[test]
pub fn ddump_analysis_cu_headers_is_2() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of myfile1.o");
        let dbg_info = elf
            .get_dwarf_section(midas::dwarf::Section::DebugInfo)
            .expect("Failed to get .debug_info");
        let cus: Vec<_> = CompilationUnitHeaderIterator::new(&dbg_info).collect();
        assert_eq!(cus.len(), 2);
        println!("{:#?}", cus);
    })
}

#[test]
pub fn parse_ddump_analysis_abbreviations() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of myfile1.o");
        let dbg_info = elf
            .get_dwarf_section(midas::dwarf::Section::DebugInfo)
            .expect("Failed to get .debug_info");
        let dbg_abbr = elf
            .get_dwarf_section(midas::dwarf::Section::DebugAbbrev)
            .expect("failed to get .debug_abbrev");

        let cu_iterator = CompilationUnitHeaderIterator::new(&dbg_info);
        let abbr_iterator = AbbreviationsTableIterator::new(&dbg_abbr, cu_iterator);
        for entries in abbr_iterator {
            println!("{:#?}", entries);
        }
    })
}

#[test]
pub fn get_program_main_address_of_ddump_analysis() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of myfile1.o");
        let debug_line = elf
            .get_dwarf_section(midas::dwarf::Section::DebugLine)
            .expect("failed to get .debug_line");

        let pub_names = elf
            .get_dwarf_section(midas::dwarf::Section::DebugPubNames)
            .expect("failed to get .debug_line");

        let debug_info = elf
            .get_dwarf_section(midas::dwarf::Section::DebugInfo)
            .expect("failed to get .debug_line");

        let abbrev_table = elf
            .get_dwarf_section(midas::dwarf::Section::DebugAbbrev)
            .expect("failed to get .debug_line");

        let low_pc = midas::dwarf::compilation_unit::find_low_pc_of("main", debug_info, pub_names, abbrev_table);
        let addr = get_addr_from_objdump_symbolname_is_exact!("ddump_analysis", "main");
        assert_eq!(low_pc, addr);
    })
}

#[test]
pub fn hardcoded_binary_test_iterators_produce_same_result() {
    assert_eq!(DEBUG_STR.len(), 236);
    let abbr_assert: std::collections::HashMap<u64, _> = attributes::parse_cu_attributes(&DEBUG_ABBREV).unwrap();

    let mut assert_coll: Vec<_> = abbr_assert.iter().map(|i| i).collect();
    assert_coll.sort_by(|(&a, _), (&b, _)| a.cmp(&b));

    let cu_iterator = CompilationUnitHeaderIterator::new(&DEBUG_INFO);
    let abbr_iterator: Vec<_> = AbbreviationsTableIterator::new(&DEBUG_ABBREV, cu_iterator).collect();
    let mut abbr_test: Vec<_> = abbr_iterator[0].iter().map(|i| i).collect();
    abbr_test.sort_by(|(&a, _), (&b, _)| a.cmp(&b));

    assert_eq!(abbr_test.len(), abbr_assert.len());

    for (a, b) in abbr_test.iter().zip(assert_coll.iter()) {
        if a.0 != b.0 {
            println!("{:?} ==\n{:?}", abbr_test, assert_coll);
        }
        println!("{}, {}", a.0, b.0);
        assert_eq!(a.0, b.0);
        println!("{:?} ==\n{:?}", a.1, b.1);
        assert_eq!(a.1, b.1);
    }
}

#[test]
pub fn parse_pubname_section_ddump_analysis() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of ddump_analysis");
        let pub_names = elf
            .get_dwarf_section(midas::dwarf::Section::DebugPubNames)
            .expect("failed to get .debug_line");

        let mut headers = midas::dwarf::pubnames::PubNameHeaderIterator::new(pub_names);

        for header in headers {
            let data_offset = header.header_bytes();
            let mut entries = midas::dwarf::pubnames::PubNameEntryIterator::new(bytereader::ConsumeReader::wrap(
                &pub_names[header.section_offset + data_offset..],
            ));
            for entry in entries {}
        }
    });
}

#[test]
pub fn find_symbol_make_todo_in_ddump_analysis() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of ddump_analysis");
        let pub_names = elf
            .get_dwarf_section(midas::dwarf::Section::DebugPubNames)
            .expect("failed to get .debug_line");

        let debug_info = elf
            .get_dwarf_section(midas::dwarf::Section::DebugInfo)
            .expect("failed to get .debug_line");

        let abbrev_table = elf
            .get_dwarf_section(midas::dwarf::Section::DebugAbbrev)
            .expect("failed to get .debug_line");
        let low_pc = midas::dwarf::compilation_unit::find_low_pc_of("make_todo", debug_info, pub_names, abbrev_table);
        assert!(low_pc.is_some());
        // todo(simon): execute dwarfdump and pull the addresses dynamically, so this can work across platforms and computers.
        if !addr2line_compare(program_path, "make_todo", low_pc.unwrap()) {
            println!(
                "Function at address 0x{:X} did not match make_todo",
                low_pc.unwrap()
            );
        }
        assert!(addr2line_compare(
            program_path,
            "make_todo",
            low_pc.unwrap()
        ),);
        // assert_eq!(low_pc, Some(0x401240));
    });
}

#[test]
pub fn find_symbol_main_in_ddump_analysis() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of ddump_analysis");
        let pub_names = elf
            .get_dwarf_section(midas::dwarf::Section::DebugPubNames)
            .expect("failed to get .debug_line");

        let debug_info = elf
            .get_dwarf_section(midas::dwarf::Section::DebugInfo)
            .expect("failed to get .debug_line");

        let abbrev_table = elf
            .get_dwarf_section(midas::dwarf::Section::DebugAbbrev)
            .expect("failed to get .debug_line");
        assert!(midas::dwarf::pubnames::find_name("motherfucker", pub_names).is_none());
        let low_pc = midas::dwarf::compilation_unit::find_low_pc_of("main", debug_info, pub_names, abbrev_table);
        println!("Low PC possibly found at {:#X?}", low_pc);
        // todo(simon): execute dwarfdump and pull the addresses dynamically, so this can work across platforms and computers.
        assert_eq!(low_pc, Some(0x4011f0));
    });
}

#[test]
pub fn run_line_number_program_of_first_debug_line_section_ddump_cpp() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of ddump_analysis");
        let pub_names = elf
            .get_dwarf_section(midas::dwarf::Section::DebugPubNames)
            .expect("failed to get .debug_pubnames");

        let debug_info = elf
            .get_dwarf_section(midas::dwarf::Section::DebugInfo)
            .expect("failed to get .debug_info");

        let abbrev_table = elf
            .get_dwarf_section(midas::dwarf::Section::DebugAbbrev)
            .expect("failed to get .debug_abbrev");

        let line_number_table = elf
            .get_dwarf_section(midas::dwarf::Section::DebugLine)
            .expect("failed to get .debug_line");

        let lnp_header = LineNumberProgramHeaderVersion4::from_bytes(8, line_number_table);
        println!("{:#X?}", lnp_header);
        let mut line_program = LineNumberProgram::new(8, line_number_table);
        let before = std::time::Instant::now();
        let data = line_program.run();
        let after = before.elapsed().as_micros();
        println!("Running line number program took {}us", after);
        let before = std::time::Instant::now();
        for res in data {
            println!("{:?}", res);
        }
        let after = before.elapsed().as_micros();

        println!("printing the data took: {}us", after);
    });
}

#[test]
pub fn parse_2_lnp_headers() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of ddump_analysis");

        // this code verifiably works, so we use this as assert case when we can
        let dbg_info = elf
            .get_dwarf_section(midas::dwarf::Section::DebugInfo)
            .expect("Failed to get .debug_info");
        let cus: Vec<_> = CompilationUnitHeaderIterator::new(&dbg_info).collect();

        let line_number_table = elf
            .get_dwarf_section(midas::dwarf::Section::DebugLine)
            .expect("failed to get .debug_line");

        let headers: Vec<_> = midas::dwarf::linenumber::ProgramHeaderIterator::new(
            line_number_table,
            midas::dwarf::linenumber::LineInstructionConfig {
                pointer_width: 8,
                opcode_base: 13,
            },
        )
        .collect();

        assert_eq!(headers.len(), cus.len());
    })
}

#[test]
pub fn parse_2_lnps() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf = midas::elf::ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of ddump_analysis");

        let line_number_table = elf
            .get_dwarf_section(midas::dwarf::Section::DebugLine)
            .expect("failed to get .debug_line");

        let header_iterator = midas::dwarf::linenumber::ProgramHeaderIterator::new(
            line_number_table,
            midas::dwarf::linenumber::LineInstructionConfig {
                pointer_width: 8,
                opcode_base: 13,
            },
        );

        let table = midas::dwarf::linenumber::TableIterator::new(
            line_number_table,
            header_iterator,
            midas::dwarf::linenumber::LineInstructionConfig {
                pointer_width: 8,
                opcode_base: 13,
            },
        );

        for mut program in table {
            let data = program.run();
            println!(
                "header include_dirs: {:#?}",
                program.header.include_directories
            );
            for res in data {
                res.debug_print(&program.header);
            }
            println!("------ line number program end ------");
        }
    })
}
