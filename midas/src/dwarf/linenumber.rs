#![allow(unused, non_camel_case_types)]

pub struct LineNumberProgramHeaderVersion5 {
    unit_length: super::InitialLengthField,
    version: u16,
    address_size: Option<u8>,
    segment_selector_size: Option<u8>,
    header_length: usize,
    instruction_length_minimum: u8,
    max_operations_per_instruction: u8,
    default_is_statement: u8,
    line_base: i8,
    line_range: u8,
    opcode_base: u8,
    standard_opcode_lengths: Vec<u8>,
    directory_entry_format_count: u8,
    directory_entry_format: Vec<(usize, usize)>,
    directories_count: usize,
    directories: Vec<String>,
    file_name_entry_format_count: u8,
    file_name_entry_format: Vec<(usize, usize)>,
    file_names_count: usize,
    file_names: Vec<String>,
}

#[derive(Debug)]
pub struct FileEntry {
    path: String,
    dir_index: usize,
    last_modified: usize,
    file_length: usize,
}

pub struct FileEntryIterator<'a> {
    data: &'a [u8],
}

impl<'a> FileEntryIterator<'a> {
    pub fn new(data: &'a [u8]) -> FileEntryIterator<'a> {
        FileEntryIterator { data }
    }
}

impl<'a> Iterator for FileEntryIterator<'a> {
    type Item = FileEntry;
    fn next(&mut self) -> Option<Self::Item> {
        if self.data.len() > 0 {
            if self.data[0] == 0 {
                self.data = &self.data[1..]; // in effect, "empty" the backing slice, because a single 0 means we've hit the last entry
                return None;
            }

            let mut reader = bytereader::ConsumeReader::wrap(&self.data);
            // todo(simon): for now we just panic when format is erroneous
            let name = reader
                .read_str()
                .expect("failed to read file_name field of FileEntry in .debug_line")
                .to_owned();
            let _ = reader.read_u8(); // read beyond null termination.. todo(simon): perhaps change this in API?
                                      // todo(simon): decide on whether we should have a "unchecked" version, where we just instantly panic
            let dir_index = reader.read_uleb128().unwrap() as _;
            let last_modified = reader.read_uleb128().unwrap() as _;
            let file_length = reader.read_uleb128().unwrap() as _;
            let a = reader.release();
            let new_start = self.data.len() - a.len();
            self.data = &self.data[new_start..];
            Some(FileEntry {
                path: name.to_owned(),
                dir_index,
                last_modified,
                file_length,
            })
        } else {
            None
        }
    }
}

#[derive(Debug)]
pub struct LineNumberProgramHeaderVersion4 {
    unit_length: super::InitialLengthField,
    version: u16,
    header_length: usize, // 4 or 8 bytes long
    instruction_length_minimum: u8,
    max_operations_per_instruction: u8,
    default_is_statement: u8,
    line_base: i8,
    line_range: u8,
    opcode_base: u8,
    standard_opcode_lengths: Vec<u8>,
    include_directories: Vec<String>,
    file_names: Vec<FileEntry>,
}

impl LineNumberProgramHeaderVersion4 {
    pub fn from_bytes(bytes: &[u8]) -> LineNumberProgramHeaderVersion4 {
        let unit_length = super::InitialLengthField::from_bytes(bytes);
        let mut reader = bytereader::ConsumeReader::wrap(&bytes[unit_length.offsets_bytes()..]);

        let version = reader.read_u16();

        let header_length = if unit_length.is_32bit() {
            reader.read_u32() as usize
        } else {
            reader.read_u64() as usize
        };

        let instruction_length_minimum = reader.read_u8();
        let max_operations_per_instruction = reader.read_u8();
        let default_is_statement = reader.read_u8();
        let line_base = reader.read_u8() as i8;
        let line_range = reader.read_u8();
        let opcode_base = reader.read_u8();
        let codes_count = (opcode_base - 1) as usize;
        let mut standard_opcode_lengths = Vec::with_capacity(codes_count);
        let slice = reader
            .read_slice(codes_count)
            .expect("failed to parse standard opcode lengths");
        unsafe {
            std::ptr::copy_nonoverlapping(
                slice.as_ptr(),
                standard_opcode_lengths.as_mut_ptr(),
                codes_count,
            );
            standard_opcode_lengths.set_len(codes_count);
        }

        let mut include_directories = vec![];
        'include_dirs: loop {
            let res = reader.read_str();
            if let Ok(s) = res {
                if s.len() == 0 {
                    break 'include_dirs;
                } else {
                    include_directories.push(s.to_owned());
                    reader.read_u8();
                }
            } else {
                break 'include_dirs;
            }
        }
        // consume last 0-byte. todo(simon): introduce another API point, that handles this for us?
        reader.read_u8();
        let file_names = FileEntryIterator::new(reader.release()).collect();

        LineNumberProgramHeaderVersion4 {
            unit_length,
            version,
            header_length,
            instruction_length_minimum,
            max_operations_per_instruction,
            default_is_statement,
            line_base,
            line_range,
            opcode_base,
            standard_opcode_lengths,
            include_directories,
            file_names,
        }
    }
    // The file & directory entries are 1-indexed
    pub fn get_dir_by_index(&self, index: usize) -> Option<&String> {
        self.include_directories.get(index.saturating_sub(1))
    }

    // The file & directory entries are 1-indexed
    pub fn get_file_by_index(&self, index: usize) -> Option<&FileEntry> {
        self.file_names.get(index.saturating_sub(1))
    }
}

use std::num::NonZeroU64;

use crate::bytereader;
pub enum LineNumberOpEncodings {
    DW_LNS_copy = 0x01,
    DW_LNS_advance_pc = 0x02,
    DW_LNS_advance_line = 0x03,
    DW_LNS_set_file = 0x04,
    DW_LNS_set_column = 0x05,
    DW_LNS_negate_stmt = 0x06,
    DW_LNS_set_basic_block = 0x07,
    DW_LNS_const_add_pc = 0x08,
    DW_LNS_fixed_advance_pc = 0x09,
    DW_LNS_set_prologue_end = 0x0a,
    DW_LNS_set_epilogue_begin = 0x0b,
    DW_LNS_set_isa = 0x0c,
}

pub enum ExtendedLineNumberOpEncodings {
    DW_LNE_end_sequence = 0x01,
    DW_LNE_set_address = 0x02,
    Reserved = 0x034,
    DW_LNE_set_discriminator = 0x04,
    DW_LNE_lo_user = 0x80,
    DW_LNE_hi_user = 0xff,
}

pub enum LineNumberHeaderEntryFormat {
    DW_LNCT_path = 0x1,
    DW_LNCT_directory_index = 0x2,
    DW_LNCT_timestamp = 0x3,
    DW_LNCT_size = 0x4,
    DW_LNCT_MD5 = 0x5,
    DW_LNCT_lo_user = 0x2000,
    DW_LNCT_hi_user = 0x3fff,
}

pub struct LineNumberInterpreter<'a> {
    instruction_stream: &'a [u8],
    state: LineNumberState,
}

impl<'a> LineNumberInterpreter<'a> {
    pub fn new(instruction_stream: &'a [u8]) -> LineNumberInterpreter {
        let r = LineNumberInterpreter {
            instruction_stream,
            state: LineNumberState::default(),
        };
        // todo(simon): set is_statement default, by parsing the line number program header
        r
    }
}

pub struct LineNumberState {
    address: usize,
    op_index: usize,
    file: usize,
    line: usize,
    column: usize,
    is_statement: bool,
    end_sequence: bool,
    prologue_end: bool,
    epilogue_begin: bool,
    ISA: u16,
    discriminator: Option<NonZeroU64>,
}

impl Default for LineNumberState {
    fn default() -> Self {
        Self {
            address: 0,
            op_index: 0,
            file: 1,
            line: 1,
            column: 0,
            is_statement: false,
            end_sequence: false,
            prologue_end: false,
            epilogue_begin: false,
            ISA: 0,
            discriminator: None,
        }
    }
}

impl LineNumberState {
    pub fn current_operation_pointer(&self) -> usize {
        // todo(simon):  The address and op_index registers, taken together, form an operation pointer that can reference any individual operation within the instruction stream.
        unimplemented!("calculation of op pointer");
    }
}
