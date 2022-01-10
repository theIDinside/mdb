use crate::bytereader;
#[allow(unused, non_camel_case_types)]
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct CompilationUnitHeader {
    unit_length: super::InitialLengthField,
    version: u16,
    unit_type: Option<u8>,
    pub address_size: u8,
    pub abbreviation_offset: usize,
}

pub enum DWARFEncoding {
    BITS32,
    BITS64,
}

pub enum DWARF {
    Version4(DWARFEncoding),
    Version5(DWARFEncoding),
}

const fn header_size_bytes(format: DWARF) -> usize {
    match format {
        DWARF::Version4(enc) => match enc {
            DWARFEncoding::BITS32 => 4 + 2 + 1 + 4,
            DWARFEncoding::BITS64 => 12 + 2 + 1 + 8,
        },
        DWARF::Version5(enc) => match enc {
            DWARFEncoding::BITS32 => 4 + 2 + 1 + 4 + 1,
            DWARFEncoding::BITS64 => 12 + 2 + 1 + 8 + 1,
        },
    }
}

impl CompilationUnitHeader {
    const DWARF4_32_SIZE: usize = header_size_bytes(DWARF::Version4(DWARFEncoding::BITS32));
    const DWARF5_32_SIZE: usize = header_size_bytes(DWARF::Version5(DWARFEncoding::BITS32));
    const DWARF4_64_SIZE: usize = header_size_bytes(DWARF::Version4(DWARFEncoding::BITS64));
    const DWARF5_64_SIZE: usize = header_size_bytes(DWARF::Version5(DWARFEncoding::BITS64));

    pub fn from_bytes(bytes: &[u8]) -> CompilationUnitHeader {
        let unit_length = super::InitialLengthField::from_bytes(bytes);
        let mut reader = bytereader::ConsumeReader::wrap(&bytes[unit_length.offsets_bytes()..]);
        let version = reader.read_u16();

        let unit_type = if version == 5 {
            Some(reader.read_u8())
        } else {
            None
        };

        let (address_size, abbreviation_offset) = match &unit_length {
            &super::InitialLengthField::Dwarf32(_) => {
                let data = reader.read_u32();
                let address_size = reader.read_u8();
                (address_size, data as usize)
            }
            &super::InitialLengthField::Dwarf64(_) => {
                let address_size = reader.read_u8();
                let data = reader.read_u64();
                (address_size, data as usize)
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

    // returns the offset from this compilation unit, to header begin of the next
    pub fn unit_length(&self) -> usize {
        match self.unit_length {
            super::InitialLengthField::Dwarf32(section_size) => section_size as usize + 4,
            super::InitialLengthField::Dwarf64(section_size) => section_size as usize + 12,
        }
    }
}

pub struct CompilationUnitHeaderIterator<'a> {
    data: &'a [u8],
}

impl<'a> CompilationUnitHeaderIterator<'a> {
    pub fn new(data: &'a [u8]) -> CompilationUnitHeaderIterator<'a> {
        CompilationUnitHeaderIterator { data }
    }
}

impl<'a> Iterator for CompilationUnitHeaderIterator<'a> {
    type Item = CompilationUnitHeader;

    fn next(&mut self) -> Option<Self::Item> {
        if self.data.len() >= CompilationUnitHeader::DWARF4_32_SIZE {
            let header = CompilationUnitHeader::from_bytes(&self.data);
            let offset = header.unit_length();
            self.data = &self.data[offset..];
            Some(header)
        } else {
            None
        }
    }
}
