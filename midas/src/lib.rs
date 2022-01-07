extern crate linuxwrapper as nixwrap;

pub mod commands;
pub mod debugger;
pub mod dwarf;
pub mod elf;
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
}

impl MidasError {
    pub fn description(&self) -> &'static str {
        match self {
            MidasError::BadUnsignedLEB128Encoding(_) => "[Bad format]: Decoding unsigned LEB128 failed",
            MidasError::BadSignedLEB128Encoding(_) => "[Bad format]: Decoding signed LEB128 failed",
            MidasError::DwarfSectionNotFound(_) => "[DWARF Error]: Section not found",
            MidasError::DwarfSectionNotRecognized => "[DWARF Error]: Section name not recognized",
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
