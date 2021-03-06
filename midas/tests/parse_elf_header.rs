use midas::{self, elf};
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

/// These kinds of tests only work on local computers. You will have to figure out these fields yourself for your platform
/// doing something like
/// ```bash
/// hexedit helloworld
/// ```
#[test]
pub fn parse_elf_header() {
    run_test(|| {
        let program_path = subjects!("helloworld");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let elf_header = midas::elf::ELFHeader::from(&object.data[..]).unwrap();
        let should_be = midas::elf::ELFHeader {
            architecture: midas::elf::Class::ELF64,
            encoding: midas::elf::DataEncoding::LSB,
            elf_version: midas::elf::Version::Current,
            os_abi: midas::elf::OperatingSystemABI::NONE_OR_SYSV,
            object_type: midas::elf::ObjectType::Executable,
            machine_type: midas::elf::Machine::X86_64,
            file_version: midas::elf::Version::Current,
            entry_point_addr: 0x00_00_00_00_00_40_10_40,
            program_header_offset: 0x00_00_00_00_00_00_00_40,
            section_header_offset: 30032,
            flags: 0x00_00_00_00,
            elf_header_size: 0x00_40,
            program_header_entry_size: 0x00_38,
            program_header_entries: 0x00_0B,
            section_header_entry_size: 0x00_40,
            section_header_entries: 0x00_23,
            section_header_string_index: 0x00_22,
        };

        assert_eq!(elf_header.architecture, should_be.architecture);
        assert_eq!(elf_header.encoding, should_be.encoding);
        assert_eq!(elf_header.elf_version, should_be.elf_version);
        assert_eq!(elf_header.os_abi, should_be.os_abi);
        assert_eq!(elf_header.object_type, should_be.object_type);
        assert_eq!(elf_header.machine_type, should_be.machine_type);
        assert_eq!(elf_header.file_version, should_be.file_version);

        assert_eq!(elf_header, should_be);

        println!("--- Parsed Header --- \n{}", elf_header);
        println!("--- Hand-written Header --- \n{}", should_be);
        let firefox_object = midas::elf::load_object(std::path::Path::new("/usr/lib/firefox/firefox")).unwrap();
        let firefox_elf_header = midas::elf::ELFHeader::from(&firefox_object.data[..]).unwrap();
        println!("---- Firefox ELF Header ----\n{}", firefox_elf_header);
    })
}

#[test]
pub fn print_program_headers_of_helloworld() {
    run_test(|| {
        let program_path = subjects!("helloworld");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let parser = elf::ParsedELF::parse_elf(&object).unwrap();
        parser.print_program_segments();
        println!("Interpreter: {}", parser.get_interpreter().unwrap());
        parser.print_section_headers();
    })
}

#[test]
pub fn print_program_headers_of_helloworld2() {
    run_test(|| {
        let program_path = subjects!("helloworld");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let parser = elf::ParsedELF::parse_elf(&object).expect("Failed to parse ELF");
        parser.print_program_segments();
        parser.print_section_headers();
    })
}

#[test]
pub fn print_ddump_headers() {
    run_test(|| {
        let program_path = subjects!("ddump_analysis");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let parser = elf::ParsedELF::parse_elf(&object).unwrap();
        parser.print_program_segments();
        println!("Interpreter: {}", parser.get_interpreter().unwrap());
        parser.print_section_headers();
    })
}

#[test]
pub fn print_info_myfile1() {
    run_test(|| {
        let program_path = subjects!("myfile1.o");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let parser = elf::ParsedELF::parse_elf(&object).unwrap();
        parser.print_program_segments();
        parser.print_section_headers();
    })
}

#[test]
pub fn print_symbol_table() {
    run_test(|| {
        let program_path = subjects!("helloworld");
        let object = midas::elf::load_object(std::path::Path::new(program_path)).unwrap();
        let parser = elf::ParsedELF::parse_elf(&object).unwrap();
        parser.symbol_table.print_unordered();
    })
}
