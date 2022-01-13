use std::str::Utf8Error;

extern crate linuxwrapper as nixwrap;

pub mod bytereader;
pub mod commands;
pub mod dwarf;
pub mod elf;
// used to live in /dwarf module, but moved here, due to wrapping reading operations in bytereader::Reader
pub mod leb128;
pub mod software_breakpoint;
pub mod target;
pub mod types;
pub mod utils;

#[derive(Debug)]
pub enum MidasError {
    BadUnsignedLEB128Encoding(usize),
    BadSignedLEB128Encoding(usize),
    DwarfSectionNotFound(dwarf::sections::Section),
    DwarfSectionNotRecognized,
    EOFNotExpected,
    ELFMagicNotFound,
    SymbolTableMalformed,
    SectionNotFound(ELFSection),
    ReaderOutOfBounds,
    AttributeParseError,
    UTF8Error {
        valid_up_to: usize,
        error_len: Option<usize>,
    },
    ErroneousAddressSize(usize),
    // to keep MidasError non-allocating, we return the OS error codes instead of the message provided by rust's stdlib
    FileOpenError(Option<i32>),
    FileReadError(Option<i32>),
}

pub use dwarf::compilation_unit::find_low_pc_of;

#[derive(Debug)]
pub enum ELFSection {
    SymbolTable,
    DWARF(dwarf::Section),
}

pub const fn tostr(elfsection: ELFSection) -> &'static str {
    match elfsection {
        ELFSection::SymbolTable => "Symbol table",
        ELFSection::DWARF(section) => match section {
            dwarf::Section::DebugAbbrev => ".debug_abbrev",
            dwarf::Section::DebugAddr => ".debug_addr",
            dwarf::Section::DebugAranges => ".debug_aranges",
            dwarf::Section::DebugCuIndex => ".debug_cu_index",
            dwarf::Section::DebugFrame => ".debug_frame",
            dwarf::Section::EhFrame => ".eh_frame",
            dwarf::Section::EhFrameHeader => ".eh_frame_hdr",
            dwarf::Section::DebugInfo => ".debug_info",
            dwarf::Section::DebugLine => ".debug_line",
            dwarf::Section::DebugLineStr => ".debug_line_str",
            dwarf::Section::DebugLoc => ".debug_loc",
            dwarf::Section::DebugLocLists => ".debug_loclists",
            dwarf::Section::DebugMacinfo => ".debug_macinfo",
            dwarf::Section::DebugMacro => ".debug_macro",
            dwarf::Section::DebugPubNames => ".debug_pubnames",
            dwarf::Section::DebugPubTypes => ".debug_pubtypes",
            dwarf::Section::DebugRanges => ".debug_ranges",
            dwarf::Section::DebugRngLists => ".debug_rnglists",
            dwarf::Section::DebugStr => ".debug_str",
            dwarf::Section::DebugStrOffsets => ".debug_str_offsets",
            dwarf::Section::DebugTuIndex => ".debug_tu_index",
            dwarf::Section::DebugTypes => ".debug_types",
        },
    }
}

impl From<Utf8Error> for MidasError {
    fn from(e: Utf8Error) -> Self {
        Self::UTF8Error {
            valid_up_to: e.valid_up_to(),
            error_len: e.error_len(),
        }
    }
}

impl MidasError {
    pub fn describe(&self) -> &'static str {
        match self {
            MidasError::BadUnsignedLEB128Encoding(_) => "[LEB128] error: Decoding unsigned LEB128 failed",
            MidasError::BadSignedLEB128Encoding(_) => "[LEB128] error: Decoding signed LEB128 failed",
            MidasError::DwarfSectionNotFound(_) => "[DWARF] error: Section not found",
            MidasError::DwarfSectionNotRecognized => "[DWARF] error: Section name not recognized",
            MidasError::EOFNotExpected => "[READ] error: Unexpectedly saw EOF",
            MidasError::ELFMagicNotFound => "[ELF] error: ELF magic number incorrect",
            MidasError::UTF8Error { .. } => "[STR] error: Invalid stream of bytes",
            MidasError::SymbolTableMalformed => "[ELF] error: Symbol table data malformed",
            MidasError::SectionNotFound(_) => "[ELF] error: Section not found.",
            MidasError::ReaderOutOfBounds => "[BYTEREADER]: Position out of bounds of slice",
            MidasError::AttributeParseError => "[DWARF]: Parsing of attributes failed",
            MidasError::ErroneousAddressSize(..) => "[DWARF]: Erroenous address size",
            MidasError::FileOpenError(..) => "[FILE]: Failed to open file",
            MidasError::FileReadError(..) => "[FILE]: Failed to read file",
        }
    }
}

impl std::fmt::Display for MidasError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.describe())
    }
}

pub type MidasSysResult<T> = Result<T, MidasError>;

#[cfg(test)]
mod tests {}
