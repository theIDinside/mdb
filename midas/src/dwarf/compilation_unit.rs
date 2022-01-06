#[allow(unused, non_camel_case_types)]
#[derive(Debug)]
pub struct CompilationUnitHeader {
    unit_length: super::InitialLengthField,
    version: u16,
    unit_type: Option<u8>,
    address_size: u8,
    abbreviation_offset: usize,
}

impl CompilationUnitHeader {
    pub fn from_bytes(bytes: &[u8]) -> CompilationUnitHeader {
        let unit_length = super::InitialLengthField::from_bytes(bytes);
        let mut offset = unit_length.offsets_bytes();
        let version = unsafe {
            let mut buf = [0u8; 2];
            std::ptr::copy_nonoverlapping(bytes.as_ptr().offset(offset as _), buf.as_mut_ptr(), 2);
            offset += 2;
            std::mem::transmute::<[u8; 2], u16>(buf)
        };

        let unit_type = if version == 5 {
            offset += 1;
            Some(bytes[offset])
        } else {
            None
        };

        let (address_size, abbreviation_offset) = unsafe {
            match &unit_length {
                &super::InitialLengthField::Dwarf32(_) => {
                    let mut buf = [0u8; 4];
                    std::ptr::copy_nonoverlapping(bytes.as_ptr().offset(offset as _), buf.as_mut_ptr(), 4);
                    let data = std::mem::transmute::<[u8; 4], u32>(buf);
                    offset += 4;
                    let address_size = bytes[offset];
                    (address_size, data as usize)
                }
                &super::InitialLengthField::Dwarf64(_) => {
                    offset += 1;
                    let address_size = bytes[offset];
                    let mut buf = [0u8; 8];
                    std::ptr::copy_nonoverlapping(bytes.as_ptr().offset(offset as _), buf.as_mut_ptr(), 8);
                    let data = std::mem::transmute::<[u8; 8], u64>(buf);
                    (address_size, data as usize)
                }
            }
        };

        CompilationUnitHeader {
            unit_length,
            version,
            unit_type,
            address_size,
            abbreviation_offset,
        }
    }

    pub fn stride(&self) -> usize {
        self.unit_length.offsets_bytes()
            + 2
            + self.unit_type.map(|_| 1).unwrap_or(0)
            + 1
            + if self.unit_length.is_32bit() { 4 } else { 8 }
    }
}
