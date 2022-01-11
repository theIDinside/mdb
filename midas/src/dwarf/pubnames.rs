use crate::bytereader;

#[derive(Debug)]
pub struct PubNameHeader {
    unit_length: super::InitialLengthField,
    pub version: u16,
    pub debug_info_offset: usize, // u32 or u64 for DWARF32 / DWARF64
    pub debug_info_length: usize, // u32 or u64 for DWARF32 / DWARF64
    // our own tracking info. Offset to where this header begins in the byte stream of .debug_pubnames section
    pub section_offset: usize,
}

impl PubNameHeader {
    pub fn from_bytes(data: &[u8], section_offset: usize) -> PubNameHeader {
        let mut reader = bytereader::ConsumeReader::wrap(&data);
        let unit_length = reader.read_initial_length();
        let version = reader.read_u16();
        let debug_info_offset = reader.read_offset();
        let debug_info_length = reader.read_offset();

        let header = PubNameHeader {
            unit_length,
            version,
            debug_info_offset,
            debug_info_length,
            section_offset,
        };
        header
    }

    pub fn header_bytes(&self) -> usize {
        if self.unit_length.is_32bit() {
            4 + 2 + 4 + 4
        } else {
            12 + 2 + 8 + 8
        }
    }

    pub fn entry_set_size(&self) -> usize {
        self.unit_length.entry_length()
    }
}

pub struct PubNameHeaderIterator<'a> {
    data: &'a [u8],
    section_offset: usize,
}

impl<'a> PubNameHeaderIterator<'a> {
    pub fn new(data: &'a [u8]) -> PubNameHeaderIterator<'a> {
        PubNameHeaderIterator {
            data,
            section_offset: 0,
        }
    }
}

impl<'a> Iterator for PubNameHeaderIterator<'a> {
    type Item = PubNameHeader;
    fn next(&mut self) -> Option<Self::Item> {
        if self.data.len() == 0 {
            return None;
        } else {
            let header = PubNameHeader::from_bytes(self.data, self.section_offset);
            self.section_offset = header.unit_length.entry_length() + header.unit_length.offsets_bytes();
            self.data = &self.data[self.section_offset..];
            Some(header)
        }
    }
}

#[derive(Debug)]
pub struct PubNameEntry {
    offset: usize,
    name: String,
}

pub struct PubNameEntryIterator<'a> {
    data: bytereader::ConsumeReader<'a>,
}

impl<'a> PubNameEntryIterator<'a> {
    pub fn new(data: bytereader::ConsumeReader<'a>) -> PubNameEntryIterator<'a> {
        PubNameEntryIterator { data }
    }
}

impl<'a> Iterator for PubNameEntryIterator<'a> {
    type Item = PubNameEntry;

    fn next(&mut self) -> Option<Self::Item> {
        if self.data.has_more() {
            let offset = self.data.read_offset();
            if offset == 0 {
                return None;
            }
            let name = self
                .data
                .read_str()
                .expect("failed to parse PubNameEntry name")
                .to_owned();
            // tood(simon): when reading strings, consume the nullbyte. this is error-prone. change this. add this todo at every place this has to be done
            self.data.read_u8();
            Some(PubNameEntry { offset, name })
        } else {
            None
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct DIEOffset {
    // offset to the compilation unit header in .debug_info section
    pub header_offset: usize,
    // relative offset from header_offset
    pub relative_entry_offset: usize,
}

impl DIEOffset {
    pub fn new(headeroffset_from_debug_info_begin: usize, relative_from_header_offset: usize) -> DIEOffset {
        DIEOffset {
            header_offset: headeroffset_from_debug_info_begin,
            relative_entry_offset: relative_from_header_offset,
        }
    }
}

/// find DIE location in .debug_info of symbol with name `name`
/// returns a tuple containing the offset into the .debug_info section that contains the CU, as well as the offset in that CU, which this name references or None
pub fn find_name(name: &str, pub_names: &[u8]) -> Option<DIEOffset> {
    for header in PubNameHeaderIterator::new(pub_names) {
        let data_offset = header.header_bytes();
        let entries = PubNameEntryIterator::new(bytereader::ConsumeReader::wrap(
            &pub_names[header.section_offset + data_offset..],
        ));
        for entry in entries {
            if entry.name == name {
                return Some(DIEOffset::new(header.debug_info_offset, entry.offset));
            }
        }
    }
    None
}
