use std::str::Utf8Error;

extern crate linuxwrapper as nixwrap;

pub mod bytereader;
pub mod commands;
pub mod debugger;
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
    UTF8Error {
        valid_up_to: usize,
        error_len: Option<usize>,
    },
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
            Self::BadUnsignedLEB128Encoding(_) => "[LEB128 FORMAT] error: Decoding unsigned LEB128 failed",
            Self::BadSignedLEB128Encoding(_) => "[LEB128 FORMAT] error: Decoding signed LEB128 failed",
            Self::DwarfSectionNotFound(_) => "[DWARF] error: Section not found",
            Self::DwarfSectionNotRecognized => "[DWARF] error: Section name not recognized",
            Self::EOFNotExpected => "[READ] error: Unexpectedly saw EOF",
            Self::ELFMagicNotFound => "[ELF] error: ELF magic number incorrect",
            Self::UTF8Error { .. } => "[UTF8] error: Invalid stream of bytes",
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
