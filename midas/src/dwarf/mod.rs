pub mod aranges;
pub mod attributes;
pub mod callframe;
pub mod die;
pub mod linenumber;
pub mod macros;
pub mod operations;
pub mod stack;
pub mod stringoffset;
pub mod tag;

pub enum InitialLengthField {
    Dwarf32(u32),
    Dwarf64(u64),
}

impl InitialLengthField {
    pub fn get(value: u32) -> InitialLengthField {
        match value {
            // means we're 64-bit, we need to read the next 8 bytes, after where ever these 4 bytes came from
            0xff_ff_ff_ff => InitialLengthField::Dwarf64(0),
            _ => InitialLengthField::Dwarf32(value),
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

    pub fn length(&self) -> usize {
        match self {
            InitialLengthField::Dwarf32(len) => *len as usize,
            InitialLengthField::Dwarf64(len) => *len as usize,
        }
    }
}
