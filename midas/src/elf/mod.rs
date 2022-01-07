#![allow(unused, non_camel_case_types)]
mod elf32;
mod elf64;
mod programheader;
mod section;
use super::dwarf;

use std::{collections::HashMap, io::Read};

#[cfg(target_arch = "x86")]
pub use elf32::*;

#[cfg(target_arch = "x86_64")]
pub use elf64::*;
use nixwrap::MidasSysResultDynamic;

use crate::{utils::midas_err, MidasSysResult};

use self::programheader::ProgramHeader;

pub struct Object {
    pub data: Vec<u8>,
    pub bytes_read: usize,
}

pub struct StringTable<'a> {
    table: &'a [u8],
}

pub struct ParsedELF<'a> {
    object: &'a Object,
    header: elf64::ELFHeader,
    section_names: Vec<String>,
    dwarf_sections: HashMap<dwarf::Section, &'a [u8]>,
    sections: HashMap<&'a str, section::Section<'a>>,
}

impl<'a> ParsedELF<'a> {
    pub fn parse_elf(obj: &'a Object) -> MidasSysResult<ParsedELF<'a>> {
        let header = elf64::ELFHeader::from(&obj.data[..])?;
        let mut sections = HashMap::new();
        let section_headers = Self::parse_section_headers(&header, obj)?;

        let mut dwarf_sections = HashMap::new();
        let mut section_name_offset_mapping = HashMap::new();

        let string_table_file_offset = section_headers
            .get(header.section_header_string_index as usize)
            .unwrap()
            .section_data_offset;

        let string_table = &obj.data[string_table_file_offset as usize..];
        let sh_ent_sz = header.section_header_entry_size as usize;
        let mut section_names = Vec::new();
        for (index, sh) in section_headers.iter().enumerate() {
            let idx = sh.string_table_index as usize;
            let str_term = string_table
                .iter()
                .skip(idx)
                .position(|&b| b == 0)
                .expect("string table entry null terminator expected.");
            let section_name = std::str::from_utf8(&string_table[idx..idx + str_term])?;
            let section_data_in_obj_f = &obj.data[sh.address_range()];
            if let Ok(section_id) = dwarf::Section::try_from(section_name) {
                dwarf_sections.insert(section_id, section_data_in_obj_f);
            }
            section_name_offset_mapping.insert(section_name.to_owned(), section_data_in_obj_f);
            section_names.push(section_name.to_string());
            sections.insert(
                section_name,
                section::Section::from_object_file(index, &obj, sh),
            );
        }
        // todo(simon): this is hacky as shit. I've done this, because I had to figure out how dwarf elf etc actually works first
        // when it's functioning, this *will* be refactored, so that we don't create unnecessary hashmaps
        let pe = ParsedELF {
            object: obj,
            header,
            section_names,
            dwarf_sections,
            sections,
        };
        Ok(pe)
    }

    pub fn get_section_data(&self, name: &str) -> Option<&[u8]> {
        self.sections.get(name).map(|sec| sec.data())
    }

    // a bit more optimized search, we don't have to hash a string first
    pub fn get_dwarf_section_data(&self, dwarf_section: dwarf::Section) -> Option<&[u8]> {
        self.dwarf_sections.get(&dwarf_section).map(|&slice| slice)
    }

    pub fn get_section_header_name(
        &'a self,
        section_header: &section::SectionHeader,
    ) -> MidasSysResultDynamic<&'a str> {
        let idx = section_header.string_table_index as usize;
        let bytes = self.string_table_data()?;
        let str_term = bytes.iter().skip(idx).position(|&b| b == 0);
        if let Some(pos) = str_term {
            std::str::from_utf8(&bytes[idx..idx + pos]).map_err(|err| err.to_string())
        } else {
            Err("Could not find string null terminator in string table".to_string())
        }
    }

    pub fn get_program_segment_header(&self, index: usize) -> Option<ProgramHeader> {
        if index < self.header.program_header_entries as _ {
            let offset = self.header.program_header_offset as usize + index * self.header.ph_entry_size();
            let slice = &self.object.data[offset..];
            unsafe {
                let ptr = slice.as_ptr() as *const libc::Elf64_Phdr;
                let ph = ProgramHeader::from_libc_repr(&*ptr, offset);
                ph.ok()
            }
        } else {
            None
        }
    }

    pub fn get_program_segment_headers_of(&self, segment_type: programheader::Type) -> Option<Vec<ProgramHeader>> {
        let mut v = vec![];
        for x in 0..self.header.program_header_entries {
            if let Some(ph) = self.get_program_segment_header(x as _) {
                if ph.ph_type == segment_type {
                    v.push(ph)
                }
            }
        }

        if v.is_empty() {
            None
        } else {
            Some(v)
        }
    }

    pub fn get_raw_segment_headers_of(&'a self, segment_type: programheader::Type) -> Vec<&'a [u8]> {
        let mut v = vec![];
        for x in 0..self.header.program_header_entries {
            if let Some(ph) = self.get_program_segment_header(x as _) {
                if ph.ph_type == segment_type {
                    let range =
                        (ph.header_object_file_offset()..ph.header_object_file_offset() + self.header.ph_entry_size());
                    v.push(&self.object.data[range])
                }
            }
        }
        v
    }

    pub fn print_program_segments(&self) {
        println!(
            "Program header entries: {}",
            self.header.program_header_entries
        );
        for x in 0..self.header.program_header_entries {
            if let Some(ph) = self.get_program_segment_header(x as _) {
                println!(
                    "Program Header Entry #{}\n------------------------\n{:?}",
                    x, ph
                );
            }
        }
    }

    pub fn print_section_headers(&self) {
        println!(
            "Section header entries: {}",
            self.header.section_header_entries
        );
        let shs = ParsedELF::parse_section_headers(&self.header, &self.object).expect("failed to get section headers");
        debug_assert_eq!(shs.len(), self.header.section_header_entries as usize);

        for (index, (name, sh)) in self.section_names.iter().zip(shs).enumerate() {
            println!("Section Header Entry #{}: {}", index, name);
            println!("{:#?}", sh);
        }
    }

    pub fn parse_section_headers(
        elf_header: &elf64::ELFHeader,
        object: &'a Object,
    ) -> MidasSysResult<Vec<section::SectionHeader>> {
        let section_header_size = elf_header.section_header_entry_size as usize;
        let start = elf_header.section_header_offset as usize;

        let end = start + (section_header_size * elf_header.sections_count());

        let mut reader = crate::bytereader::Reader::wrap(&object.data[start..end]);
        let mut section_headers = vec![];
        for x in 0..elf_header.sections_count() {
            let slice = reader.read_slice(section_header_size)?;
            unsafe {
                let ptr = slice.as_ptr() as *const libc::Elf64_Shdr;
                let section_header = section::SectionHeader::from_libc_repr(&*ptr);
                section_headers.push(section_header);
            }
        }
        Ok(section_headers)
    }

    pub fn get_interpreter(&'a self) -> MidasSysResultDynamic<&'a str> {
        let interpreter_header = self
            .get_program_segment_headers_of(programheader::Type::ProgramInterpreter)
            .ok_or("Could not find interpreter headers".to_string())?;
        let ph = unsafe { interpreter_header.get(0).unwrap_unchecked() };

        std::str::from_utf8(&self.object.data[ph.file_offset as usize..(ph.file_offset + ph.file_size) as usize])
            .map_err(|err| err.to_string())
    }

    pub fn string_table_data(&self) -> MidasSysResultDynamic<&'a [u8]> {
        let section_headers =
            Self::parse_section_headers(&self.header, &self.object).expect("Failed to parse ELF header");
        let string_table_file_offset = section_headers
            .get(self.header.section_header_string_index as usize)
            .unwrap()
            .section_data_offset;
        Ok(&self.object.data[string_table_file_offset as usize..])
    }

    pub fn get_dwarf_section(&self, dwarf_section: super::dwarf::sections::Section) -> MidasSysResult<&[u8]> {
        self.dwarf_sections
            .get(&dwarf_section)
            .map(|f| *f)
            .ok_or(crate::MidasError::DwarfSectionNotRecognized)
    }
}

impl Object {
    pub fn new(data: Vec<u8>, bytes_read: usize) -> Self {
        Self {
            data: data,
            bytes_read: bytes_read,
        }
    }
}

pub fn load_object(path: &std::path::Path) -> MidasSysResultDynamic<Object> {
    let mut buf = vec![];
    let mut f = std::fs::OpenOptions::new()
        .read(true)
        .create_new(false)
        .open(path)
        .map_err(midas_err)?;
    let file_size = f.metadata().map_err(midas_err)?.len();
    buf.reserve(file_size as _);
    let bytes_read = f.read_to_end(&mut buf).map_err(midas_err)?;
    Ok(Object::new(buf, bytes_read))
}
