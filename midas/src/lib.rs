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
    UTF8Error {
        valid_up_to: usize,
        error_len: Option<usize>,
    },
}

#[derive(Debug)]
pub enum ELFSection {
    SymbolTable,
}

pub const fn tostr(elfsection: ELFSection) -> &'static str {
    match elfsection {
        ELFSection::SymbolTable => "Symbol table",
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
    pub fn description(&self) -> &'static str {
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
        }
    }
}

impl std::fmt::Display for MidasError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.description())
    }
}

pub type MidasSysResult<T> = Result<T, MidasError>;

#[cfg(test)]
mod tests {}
