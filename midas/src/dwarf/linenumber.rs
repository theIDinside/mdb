#![allow(unused, non_camel_case_types, non_upper_case_globals)]
use crate::{bytereader, dwarf::linenumber::encodings::LineNumberOp, MidasError};
use std::num::{NonZeroU128, NonZeroU64};

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

impl FileEntry {
    pub fn new(path: String, dir_index: usize, last_modified: usize, file_length: usize) -> FileEntry {
        FileEntry {
            path,
            dir_index,
            last_modified,
            file_length,
        }
    }
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
    pub unit_length: super::InitialLengthField,
    pub version: u16,
    pub header_length: usize, // 4 or 8 bytes long
    pub instruction_length_minimum: u8,
    pub max_operations_per_instruction: u8,
    pub default_is_statement: bool,
    pub line_base: i8,
    pub line_range: u8,
    pub opcode_base: u8,
    pub standard_opcode_lengths: Vec<u8>,
    pub include_directories: Vec<String>,
    pub file_names: Vec<FileEntry>,
    pub pointer_width: u8,
}

impl LineNumberProgramHeaderVersion4 {
    pub fn from_bytes(address_size: u8, bytes: &[u8]) -> LineNumberProgramHeaderVersion4 {
        let mut reader = bytereader::ConsumeReader::wrap(&bytes);
        let unit_length = reader.read_initial_length();
        let version = reader.read_u16();

        let header_length = if unit_length.is_32bit() {
            reader.read_u32() as usize
        } else {
            reader.read_u64() as usize
        };

        let instruction_length_minimum = reader.read_u8();
        let max_operations_per_instruction = reader.read_u8();
        let default_is_statement = reader.read_u8() != 0;
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
                    // tood(simon): when reading strings, consume the nullbyte. this is error-prone. change this. add this todo at every place this has to be done
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
            pointer_width: address_size,
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

    pub fn line_number_program_begins(&self) -> usize {
        match self.unit_length {
            super::InitialLengthField::Dwarf32(len) => todo!(),
            super::InitialLengthField::Dwarf64(len) => todo!(),
        }
    }
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
    basic_block: bool,
    end_sequence: bool,
    prologue_end: bool,
    epilogue_begin: bool,
    isa: u16,
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
            basic_block: false,
            end_sequence: false,
            prologue_end: false,
            epilogue_begin: false,
            isa: 0,
            discriminator: None,
        }
    }
}

impl LineNumberState {
    pub fn current_operation_pointer(&self) -> usize {
        // todo(simon):  The address and op_index registers, taken together, form an operation pointer that can reference any individual operation within the instruction stream.
        unimplemented!("calculation of op pointer");
    }

    fn reset(&mut self, is_statement: bool) {
        let mut state = LineNumberState::default();
        state.is_statement = is_statement;
        *self = state;
    }
}

pub struct ComputationResult {
    address: usize,
    op_index: u16,
    file: u32,
    line: u32,
    column: u32,
    description: u8,
    isa: u16,
    discriminator: Option<NonZeroU64>,
}

pub fn description(
    is_statement: bool,
    basic_block: bool,
    end_sequence: bool,
    prologue_end: bool,
    epilogue_begin: bool,
) -> &'static str {
    match (
        is_statement,
        basic_block,
        end_sequence,
        prologue_end,
        epilogue_begin,
    ) {
        (true, true, true, true, true) => "NS BB ET PE EB, ",
        (true, true, true, true, false) => "NS BB ET PE, ",
        (true, true, true, false, true) => "NS BB ET EB, ",
        (true, true, true, false, false) => "NS BB ET, ",
        (true, true, false, true, true) => "NS BB PE EB, ",
        (true, true, false, true, false) => "NS BB PE, ",
        (true, true, false, false, true) => "NS BB EB, ",
        (true, true, false, false, false) => "NS BB, ",
        (true, false, true, true, true) => "NS ET PE EB, ",
        (true, false, true, true, false) => "NS ET PE, ",
        (true, false, true, false, true) => "NS ET EB, ",
        (true, false, true, false, false) => "NS ET, ",
        (true, false, false, true, true) => "NS PE EB, ",
        (true, false, false, true, false) => "NS PE, ",
        (true, false, false, false, true) => "NS EB, ",
        (true, false, false, false, false) => "NS, ",
        (false, true, true, true, true) => "BB ET PE EB, ",
        (false, true, true, true, false) => "BB ET PE, ",
        (false, true, true, false, true) => "BB ET EB, ",
        (false, true, true, false, false) => "BB ET, ",
        (false, true, false, true, true) => "BB PE EB, ",
        (false, true, false, true, false) => "BB PE, ",
        (false, true, false, false, true) => "BB EB, ",
        (false, true, false, false, false) => "BB, ",
        (false, false, true, true, true) => "ET PE EB, ",
        (false, false, true, true, false) => "ET PE, ",
        (false, false, true, false, true) => "ET EB, ",
        (false, false, true, false, false) => "ET, ",
        (false, false, false, true, true) => "PE EB, ",
        (false, false, false, true, false) => "PE, ",
        (false, false, false, false, true) => "EB, ",
        (false, false, false, false, false) => "",
    }
}

pub fn description2(value: u8) -> &'static str {
    match value & 0x1f {
        0b11111 => "NS BB ET PE EB, ",
        0b11110 => "NS BB ET PE, ",
        0b11101 => "NS BB ET EB, ",
        0b11100 => "NS BB ET, ",
        0b11011 => "NS BB PE EB, ",
        0b11010 => "NS BB PE, ",
        0b11001 => "NS BB EB, ",
        0b11000 => "NS BB, ",
        0b10111 => "NS ET PE EB, ",
        0b10110 => "NS ET PE, ",
        0b10101 => "NS ET EB, ",
        0b10100 => "NS ET, ",
        0b10011 => "NS PE EB, ",
        0b10010 => "NS PE, ",
        0b10001 => "NS EB, ",
        0b10000 => "NS, ",
        0b01111 => "BB ET PE EB, ",
        0b01110 => "BB ET PE, ",
        0b01100 => "BB ET EB, ",
        0b01100 => "BB ET, ",
        0b01011 => "BB PE EB, ",
        0b01011 => "BB PE, ",
        0b01001 => "BB EB, ",
        0b01000 => "BB, ",
        0b00111 => "ET PE EB, ",
        0b00110 => "ET PE, ",
        0b00101 => "ET EB, ",
        0b00100 => "ET, ",
        0b00011 => "PE EB, ",
        0b00010 => "PE, ",
        0b00001 => "EB, ",
        _ => "",
    }
}

impl std::fmt::Debug for ComputationResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        /*
        let (is_statement, basic_block, end_sequence, prologue_end, epilogue_begin) = (
            self.description & 0b10000 != 0,
            self.description & 0b1000 != 0,
            self.description & &0b100 != 0,
            self.description & 0b10 != 0,
            self.description & 0b1 != 0,
        );

        let descrip: &'static str = description(
            is_statement,
            basic_block,
            end_sequence,
            prologue_end,
            epilogue_begin,
        );
        */

        let descrip = description2(self.description);

        write!(
            f,
            "0x{:016X} [{:>3}, {:>2}], {}File id: {uri}",
            self.address,
            self.line,
            self.column,
            descrip,
            uri = self.file
        )
    }
}

impl From<&LineNumberState> for ComputationResult {
    fn from(state: &LineNumberState) -> Self {
        ComputationResult::new(
            state.address,
            state.op_index as _,
            state.file as _,
            state.line as _,
            state.column as _,
            state.is_statement,
            state.basic_block,
            state.end_sequence,
            state.prologue_end,
            state.epilogue_begin,
            state.isa,
            state.discriminator,
        )
    }
}

impl ComputationResult {
    pub fn new(
        address: usize,
        op_index: u16,
        file: u32,
        line: u32,
        column: u32,
        is_statement: bool,
        basic_block: bool,
        end_sequence: bool,
        prologue_end: bool,
        epilogue_begin: bool,
        isa: u16,
        discriminator: Option<NonZeroU64>,
    ) -> ComputationResult {
        ComputationResult {
            address,
            op_index,
            file,
            line,
            column,
            description: encode_description(
                is_statement,
                basic_block,
                end_sequence,
                prologue_end,
                epilogue_begin,
            ),
            isa,
            discriminator,
        }
    }
}

pub fn encode_description(
    is_statement: bool,
    basic_block: bool,
    end_sequence: bool,
    prologue_end: bool,
    epilogue_begin: bool,
) -> u8 {
    (is_statement as u8) << 4
        | (basic_block as u8) << 3
        | (end_sequence as u8) << 2
        | (prologue_end as u8) << 1
        | (epilogue_begin as u8)
}

pub struct LineNumberProgram<'a> {
    header: LineNumberProgramHeaderVersion4,
    state: LineNumberState,
    sec_data: &'a [u8],
}

impl<'a> LineNumberProgram<'a> {
    pub fn new(address_size: u8, debug_line_section: &'a [u8]) -> LineNumberProgram<'a> {
        let header = LineNumberProgramHeaderVersion4::from_bytes(address_size, debug_line_section);
        let total_length = header.unit_length.entry_length() + 4;
        let mut state = LineNumberState::default();
        state.is_statement = header.default_is_statement;
        let pre_header_len_size = 2 + 4 + 4;
        let instructions_offset_from_debug_line_section = pre_header_len_size + header.header_length;

        let sec_data = &debug_line_section[instructions_offset_from_debug_line_section..total_length];
        LineNumberProgram {
            header,
            state,
            sec_data,
        }
    }

    pub fn run(&mut self) -> Vec<ComputationResult> {
        let mut v = vec![];
        let mut reader = bytereader::ConsumeReader::wrap(self.sec_data);
        // values that the closures will capture
        let line_range = self.header.line_range;
        let line_base = self.header.line_base;
        let opcode_base = self.header.opcode_base;
        let min_instruction_length = self.header.instruction_length_minimum;
        let max_ops_per_instruction = self.header.max_operations_per_instruction;

        let adjust_opcode = |opcode: u8| opcode.wrapping_sub(opcode_base);

        let operation_advance = |opcode| adjust_opcode(opcode) / line_range;

        let new_address = |address, op_index, opcode| {
            address
                + (min_instruction_length * ((op_index + operation_advance(opcode)) / max_ops_per_instruction)) as usize
        };

        let new_op_index =
            |op_index, opcode| (op_index + operation_advance(adjust_opcode(opcode))) % max_ops_per_instruction;

        let line_increment = |opcode| (line_base + (adjust_opcode(opcode) % line_range) as i8);

        for instruction in LineInstructionIterator::new(
            reader,
            LineInstructionConfig {
                pointer_width: self.header.pointer_width,
                opcode_base: self.header.opcode_base,
            },
        ) {
            match instruction {
                // standard operations
                LineInstruction::AppendRow => {
                    let r = ComputationResult::from(&self.state);
                    self.state.discriminator = None;
                    self.state.prologue_end = false;
                    self.state.epilogue_begin = false;
                    self.state.basic_block = false;
                    v.push(r);
                }
                LineInstruction::AdvancePC(steps) => {
                    self.state.address += steps;
                }
                LineInstruction::AdvanceLine(lines) => {
                    self.state.line = if lines < 0 {
                        self.state.line.wrapping_sub(lines.abs() as usize)
                    } else {
                        self.state.line.wrapping_add(lines as usize)
                    }
                }
                LineInstruction::SetFile(file_id) => self.state.file = file_id,
                LineInstruction::SetColumn(col) => self.state.column = col,
                LineInstruction::NegateIsStatement => self.state.is_statement = !self.state.is_statement,
                LineInstruction::SetBasicBlock => self.state.basic_block = true,
                LineInstruction::ConstAddPc => {
                    let adjusted_opcode = 255 - self.header.opcode_base;
                    self.state.address = new_address(self.state.address, self.state.op_index as u8, 255u8);
                    self.state.op_index = new_op_index(self.state.op_index as u8, 255) as usize;
                }
                LineInstruction::FixedAdvancePC(advance) => {
                    self.state.address += advance as usize;
                    self.state.op_index = 0;
                }
                LineInstruction::SetPrologueEnd => self.state.prologue_end = true,
                LineInstruction::SetEpilogueBegin => self.state.epilogue_begin = true,
                LineInstruction::SetISA(isa) => self.state.isa = isa as _,

                // special operations
                LineInstruction::Special(code) => {
                    let line_delta = line_increment(code as u8);
                    self.state.line = if line_delta < 0 {
                        self.state.line.saturating_sub(line_delta.abs() as _)
                    } else {
                        self.state.line.saturating_add(line_delta as _)
                    };
                    self.state.address = new_address(self.state.address, self.state.op_index as u8, code as u8);
                    let r = ComputationResult::from(&self.state);
                    self.state.discriminator = None;
                    self.state.prologue_end = false;
                    self.state.epilogue_begin = false;
                    self.state.basic_block = false;
                    v.push(r);
                }
                // extended operations
                LineInstruction::SetEndSequence => {
                    self.state.end_sequence = true;
                    let r = ComputationResult::from(&self.state);
                    self.state.reset(self.header.default_is_statement);
                    v.push(r);
                }
                LineInstruction::SetAddress(addr) => {
                    self.state.address = addr;
                    self.state.op_index = 0;
                }
                LineInstruction::DefineFile {
                    path,
                    directory_index,
                    last_modified,
                    file_length,
                } => {
                    self.header.file_names.push(FileEntry::new(
                        path,
                        directory_index,
                        last_modified,
                        file_length,
                    ));
                }
                LineInstruction::SetDiscriminator(discriminator) => {
                    self.state.discriminator = NonZeroU64::new(discriminator as u64);
                }
                // something we don't recognized
                LineInstruction::Unrecognized(_) => {
                    // no-op
                }
            }
        }
        v
    }
}

pub struct LineInstructionIterator<'a> {
    reader: bytereader::ConsumeReader<'a>,
    config: LineInstructionConfig,
}

impl<'a> LineInstructionIterator<'a> {
    pub fn new(reader: bytereader::ConsumeReader<'a>, config: LineInstructionConfig) -> LineInstructionIterator<'a> {
        LineInstructionIterator { reader, config }
    }
}

impl<'a> Iterator for LineInstructionIterator<'a> {
    type Item = LineInstruction;
    fn next(&mut self) -> Option<Self::Item> {
        if self.reader.has_more() {
            if let Ok(ins) = LineInstruction::parse_v4(self.config, &mut self.reader) {
                Some(ins)
            } else {
                None
            }
        } else {
            None
        }
    }
}

#[derive(Debug)]
pub enum LineInstruction {
    // standard operations
    AppendRow,
    AdvancePC(usize),
    AdvanceLine(isize),
    SetFile(usize),
    SetColumn(usize),
    NegateIsStatement,
    SetBasicBlock,
    ConstAddPc,
    FixedAdvancePC(u16),
    SetPrologueEnd,
    SetEpilogueBegin,
    SetISA(usize),
    // extended operations
    SetEndSequence,
    SetAddress(usize),
    DefineFile {
        path: String,
        directory_index: usize,
        last_modified: usize,
        file_length: usize,
    },
    SetDiscriminator(usize),
    Unrecognized(usize),
    // Special operation
    Special(usize),
}

#[derive(Clone, Copy)]
pub struct LineInstructionConfig {
    pub pointer_width: u8,
    pub opcode_base: u8,
}

impl LineInstruction {
    pub fn parse_v4(
        config: LineInstructionConfig,
        reader: &mut bytereader::ConsumeReader,
    ) -> crate::MidasSysResult<LineInstruction> {
        let opcode = reader.read_u8();
        if opcode == 0 {
            // extended opcodes begin with 0
            let extetended_size = reader.read_uleb128()?;
            let opcode = reader.read_u8();
            match encodings::ExtendedLineNumberOp(opcode) {
                encodings::EndSequence => Ok(LineInstruction::SetEndSequence),
                encodings::SetAddress => {
                    let addr = match config.pointer_width {
                        1 => reader.read_u8() as usize,
                        2 => reader.read_u16() as usize,
                        4 => reader.read_u32() as usize,
                        8 => reader.read_u64() as usize,
                        x => return Err(MidasError::ErroneousAddressSize(x as usize)),
                    };
                    Ok(LineInstruction::SetAddress(addr as usize))
                }
                encodings::DefineFile => {
                    let file = reader.read_str()?.to_owned();
                    reader.read_u8();
                    let directory_index = reader.read_uleb128()? as usize;
                    let last_modified = reader.read_uleb128()? as usize;
                    let file_length = reader.read_uleb128()? as usize;

                    Ok(LineInstruction::DefineFile {
                        path: file,
                        directory_index,
                        last_modified,
                        file_length,
                    })
                }
                encodings::SetDiscriminator => {
                    let dis = reader.read_uleb128()? as usize;
                    Ok(LineInstruction::SetDiscriminator(dis))
                }
                encodings::ExtendedLineNumberOp(code) => Ok(LineInstruction::Unrecognized(code as usize)),
            }
        } else {
            match encodings::LineNumberOp(opcode) {
                encodings::Copy => Ok(LineInstruction::AppendRow),
                encodings::AdvancePC => {
                    let operand = reader.read_uleb128()?;
                    Ok(LineInstruction::AdvancePC(operand as usize))
                }
                encodings::AdvanceLine => {
                    let operand = reader.read_ileb128()?;
                    Ok(LineInstruction::AdvanceLine(operand as isize))
                }
                encodings::SetFile => {
                    let operand = reader.read_uleb128()?;
                    Ok(LineInstruction::SetFile(operand as usize))
                }
                encodings::SetColumn => {
                    let operand = reader.read_uleb128()?;
                    Ok(LineInstruction::SetColumn(operand as usize))
                }
                encodings::NegateIsStatement => Ok(LineInstruction::NegateIsStatement),
                encodings::SetBasicBlock => Ok(LineInstruction::SetBasicBlock),
                encodings::ConstAddPC => Ok(LineInstruction::ConstAddPc),
                encodings::FixedAdvancePC => {
                    let word = reader.read_u16();
                    Ok(LineInstruction::FixedAdvancePC(word))
                }
                encodings::SetPrologueEnd => Ok(LineInstruction::SetPrologueEnd),
                encodings::SetISA => {
                    let isa_enc = reader.read_uleb128()?;
                    Ok(LineInstruction::SetISA(isa_enc as usize))
                }
                encodings::LineNumberOp(code) => {
                    if code >= config.opcode_base {
                        Ok(LineInstruction::Special(code as _))
                    } else {
                        Ok(LineInstruction::Unrecognized(code as _))
                    }
                }
            }
        }
    }
}

macro_rules! LNEncoding {
    ($struct_name:ident($struct_type:ty) { $($name:ident = $val:expr),+ $(,)? }) => {
        #[derive(Debug, PartialEq, PartialOrd, Ord, Eq, Clone, Copy)]
        pub struct $struct_name(pub $struct_type);

        $(
            #[allow(non_upper_case_globals)]
            pub const $name: $struct_name = $struct_name($val);
        )+
    };
}

pub mod encodings {
    LNEncoding!(
        LineNumberOp(u8) {
            Copy = 0x01,
            AdvancePC = 0x02,
            AdvanceLine = 0x03,
            SetFile = 0x04,
            SetColumn = 0x05,
            NegateIsStatement = 0x06,
            SetBasicBlock = 0x07,
            ConstAddPC = 0x08,
            FixedAdvancePC = 0x09,
            SetPrologueEnd = 0x0a,
            SetEpilogueBegin = 0x0b,
            SetISA = 0x0c,
        }
    );

    LNEncoding!(
        ExtendedLineNumberOp(u8) {
            EndSequence = 0x1,
            SetAddress = 0x2,
            DefineFile = 0x3,
            Reserved = 0x34,
            SetDiscriminator = 0x4,
            LO_User = 0x80,
            HI_User = 0xff
        }
    );
}
