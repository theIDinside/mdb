use crate::{
    bytereader,
    dwarf::{attributes::parse_attribute, Encoding},
    types::{Index, SectionPointer},
    MidasError, MidasSysResult, ParseError,
};

use super::{attributes::parse_cu_attributes, pubnames::DIEOffset, Format};
#[allow(unused, non_camel_case_types)]
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct CompilationUnitHeader {
    // exact data represented in byte stream
    unit_length: super::InitialLengthField,
    version: u16,
    unit_type: Option<u8>,
    pub address_size: u8,
    pub abbreviation_offset: usize,
    // our own data
    pub cu_data_start: usize,
    pub section_offset: Option<usize>,
}

pub struct CompilationUnit {
    /// this compilation unit's header
    header: CompilationUnitHeader,
    /// the compilation unit index; it's index into the .debug_info section
    cu_index: Index,
    /// Section pointer to the actual compilation unit data; not including the header itself.
    data: SectionPointer,
}

pub struct CompilationUnitIterator<'a> {
    reader: bytereader::ConsumeReader<'a>,
    index: usize,
}

impl<'a> CompilationUnitIterator<'a> {
    pub fn new(data: bytereader::ConsumeReader<'a>) -> CompilationUnitIterator<'a> {
        CompilationUnitIterator {
            reader: data,
            index: 0,
        }
    }
}

impl<'a> Iterator for CompilationUnitIterator<'a> {
    type Item = CompilationUnit;
    fn next(&mut self) -> Option<Self::Item> {
        if !self.reader.has_more() {
            return None;
        }
        let compilation_unit =
            CompilationUnit::read_from(Index(self.index), &mut self.reader).expect("failed to read compilation unit");
        self.index += 1;
        Some(compilation_unit)
    }
}

impl CompilationUnit {
    pub fn read_from(index: Index, reader: &mut bytereader::ConsumeReader) -> MidasSysResult<CompilationUnit> {
        let header = reader.dispatch_read(CompilationUnitHeader::read_from)?;
        let encoding = header.encoding();
        let cu_length = header.unit_length() - header.header_size();
        if reader.len() < cu_length {
            return Err(MidasError::ParseError(ParseError::CompilationUnit));
        }
        let data = SectionPointer::from_slice(&reader.read_slice(cu_length)?);
        Ok(CompilationUnit {
            header,
            cu_index: index,
            data,
        })
    }
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

#[allow(unused)]
impl CompilationUnitHeader {
    pub const DWARF4_32_SIZE: usize = header_size_bytes(DWARF::Version4(DWARFEncoding::BITS32));
    pub const DWARF5_32_SIZE: usize = header_size_bytes(DWARF::Version5(DWARFEncoding::BITS32));
    pub const DWARF4_64_SIZE: usize = header_size_bytes(DWARF::Version4(DWARFEncoding::BITS64));
    pub const DWARF5_64_SIZE: usize = header_size_bytes(DWARF::Version5(DWARFEncoding::BITS64));

    pub fn read_from(reader: &mut bytereader::ConsumeReader) -> MidasSysResult<CompilationUnitHeader> {
        if reader.len() < CompilationUnitHeader::DWARF5_64_SIZE {
            return Err(MidasError::ParseError(ParseError::CompilationUnitHeader));
        }
        let unit_length = reader.read_initial_length();
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
        let cu_data_start = unit_length.offsets_bytes();
        Ok(CompilationUnitHeader {
            unit_length,
            version,
            unit_type,
            address_size,
            abbreviation_offset,
            cu_data_start,
            section_offset: None,
        })
    }

    pub fn from_bytes(bytes: &[u8]) -> CompilationUnitHeader {
        let mut reader = bytereader::ConsumeReader::wrap(&bytes);
        let unit_length = reader.read_initial_length();
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
        let cu_data_start = unit_length.offsets_bytes();
        CompilationUnitHeader {
            unit_length,
            version,
            unit_type,
            address_size,
            abbreviation_offset,
            cu_data_start,
            section_offset: None,
        }
    }

    pub fn header_size(&self) -> usize {
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

    pub fn encoding(&self) -> Encoding {
        match self.unit_length {
            super::InitialLengthField::Dwarf32(_) => Encoding::new(self.address_size, Format::DWARF32, self.version),
            super::InitialLengthField::Dwarf64(_) => Encoding::new(self.address_size, Format::DWARF64, self.version),
        }
    }
}

pub struct CompilationUnitHeaderIterator<'a> {
    data: &'a [u8],
    bytes_travelled: usize,
}

impl<'a> CompilationUnitHeaderIterator<'a> {
    pub fn new(data: &'a [u8]) -> CompilationUnitHeaderIterator<'a> {
        CompilationUnitHeaderIterator {
            data,
            bytes_travelled: 0,
        }
    }
}

impl<'a> Iterator for CompilationUnitHeaderIterator<'a> {
    type Item = CompilationUnitHeader;

    fn next(&mut self) -> Option<Self::Item> {
        if self.data.len() >= CompilationUnitHeader::DWARF4_32_SIZE {
            let mut header = CompilationUnitHeader::from_bytes(&self.data);
            header.section_offset = Some(self.bytes_travelled);
            let offset = header.unit_length();
            self.bytes_travelled += offset;
            self.data = &self.data[offset..];
            Some(header)
        } else {
            None
        }
    }
}

pub fn find_low_pc_of(name: &str, debug_info: &[u8], debug_names: &[u8], abbr_table: &[u8]) -> Option<usize> {
    super::pubnames::find_name(name, debug_names).and_then(
        |DIEOffset {
             header_offset,
             relative_entry_offset,
         }| {
            let cu_header = CompilationUnitHeader::from_bytes(&debug_info[header_offset..]);
            let mut die_reader = bytereader::ConsumeReader::wrap(&debug_info[header_offset + relative_entry_offset..]);
            let abbrev_code = die_reader.read_uleb128().unwrap();
            let attr = parse_cu_attributes(&abbr_table[cu_header.abbreviation_offset..]).unwrap();

            let encoding = cu_header.encoding();
            attr.get(&abbrev_code).and_then(|item| {
                for (attribute, form) in item.attrs_list.iter() {
                    // we *must* parse the attribute, before checking that it's the one we want; because we want to move the byte stream along, if we don't parse it we either;
                    // A: don't move the bytestream (reader) along or
                    // B: we don't move it along correctly, since we can't know beforehand how long the individual fields will be, unfortunately a design DWARF has chosen.
                    let parsed_attr = parse_attribute(&mut die_reader, encoding, (*attribute, *form));
                    if parsed_attr.attribute == crate::dwarf::attributes::Attribute::DW_AT_low_pc {
                        if let crate::dwarf::attributes::AttributeValue::Address(addr) = parsed_attr.value {
                            return Some(addr);
                        }
                    }
                }
                None
            })
        },
    )
}
