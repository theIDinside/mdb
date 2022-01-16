pub mod address_table;
pub mod aranges;
pub mod attributes;
pub mod callframe;
pub mod compilation_unit;
pub mod die;
pub mod linenumber;
pub mod loclist;
pub mod macros;
pub mod operations;
pub mod pubnames;
pub mod range_list;
pub mod sections;
pub mod stack;
pub mod stringoffset;
pub mod tag;

pub use sections::*;

use crate::{bytereader, MidasError, MidasSysResult};

use self::compilation_unit::{DWARFEncoding, DWARF};

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum InitialLengthField {
    Dwarf32(u32),
    Dwarf64(u64),
}

#[derive(Copy, Clone)]
#[repr(u8)]
pub enum Format {
    DWARF32 = 4,
    DWARF64 = 8,
}

#[derive(Clone, Copy)]
pub struct Encoding {
    pub pointer_width: u8,
    pub format: Format,
    pub version: u16,
}

impl Encoding {
    pub fn to_dwarf_format(&self) -> DWARF {
        match (self.version, self.pointer_width) {
            (4, 4) => DWARF::Version4(DWARFEncoding::BITS32),
            (4, 8) => DWARF::Version4(DWARFEncoding::BITS64),
            (5, 4) => DWARF::Version5(DWARFEncoding::BITS32),
            (5, 8) => DWARF::Version5(DWARFEncoding::BITS64),
            _ => panic!(
                "Version: {}. Pointer size: {} - Unknown DWARF encoding",
                self.version, self.pointer_width
            ),
        }
    }

    pub fn new(pointer_width: u8, format: Format, version: u16) -> Encoding {
        Encoding {
            pointer_width,
            format,
            version,
        }
    }
}

impl InitialLengthField {
    pub fn get(value: u32) -> InitialLengthField {
        match value {
            // means we're 64-bit, we need to read the next 8 bytes, after where ever these 4 bytes came from
            0xff_ff_ff_ff => InitialLengthField::Dwarf64(0),
            _ => InitialLengthField::Dwarf32(value),
        }
    }

    pub fn read(reader: &mut bytereader::ConsumeReader) -> MidasSysResult<InitialLengthField> {
        if reader.len() < 12 {
            return Err(MidasError::InitialLengthField);
        }
        let dword = reader.read_u32();
        match dword {
            0xff_ff_ff_ff => Ok(InitialLengthField::Dwarf64(reader.read_u64())),
            _ => Ok(InitialLengthField::Dwarf32(dword)),
        }
    }

    /// pre-condition: bytes.len() >= 12
    pub fn from_bytes(bytes: &[u8]) -> InitialLengthField {
        let dword = unsafe {
            let mut value: [u8; 4] = [0; 4];
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), value.as_mut_ptr(), 4);
            std::mem::transmute(value)
        };

        debug_assert!(bytes.len() >= 12, "If you fucked this up, it's on you");
        let mut length_field = InitialLengthField::get(dword);
        match &mut length_field {
            Self::Dwarf32(_) => length_field,
            Self::Dwarf64(ref mut none) => {
                *none = unsafe {
                    let mut value: [u8; 8] = [0; 8];
                    std::ptr::copy_nonoverlapping(bytes.as_ptr().offset(4), value.as_mut_ptr(), 8);
                    std::mem::transmute(value)
                };
                length_field
            }
        }
    }

    pub fn offsets_bytes(&self) -> usize {
        match self {
            InitialLengthField::Dwarf32(_) => 4,
            InitialLengthField::Dwarf64(_) => 12,
        }
    }

    pub fn is_32bit(&self) -> bool {
        match &self {
            InitialLengthField::Dwarf32(_) => true,
            InitialLengthField::Dwarf64(_) => false,
        }
    }

    pub fn entry_length(&self) -> usize {
        match &self {
            InitialLengthField::Dwarf32(len) => *len as usize,
            InitialLengthField::Dwarf64(len) => *len as usize,
        }
    }
}

// "public API that we would need"
#[allow(unused)]
pub fn evaluate_context(address: usize) -> () {
    todo!("We should be able to say; build state from 0x0ffa293ff or something like that. Produce all locals, names, etc from that context.")
}

pub fn parse_abbreviations_table<'a>(dbg_info: &'a [u8], dbg_abbr: &'a [u8]) -> impl Iterator + 'a {
    let cu_iterator = compilation_unit::CompilationUnitHeaderIterator::new(&dbg_info);
    let abbr_iterator = attributes::AbbreviationsTableIterator::new(&dbg_abbr, cu_iterator);
    abbr_iterator
}
