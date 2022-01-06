#![allow(unused, non_camel_case_types)]
mod elf32;
mod elf64;
mod programheader;
mod sectionheader;

use std::{collections::HashMap, io::Read};

#[cfg(target_arch = "x86")]
pub use elf32::*;

#[cfg(target_arch = "x86_64")]
pub use elf64::*;
use nixwrap::MidasSysResultDynamic;

use crate::utils::midas_err;

use self::programheader::ProgramHeader;

pub struct Object {
    pub data: Vec<u8>,
    pub bytes_read: usize,
}

pub struct StringTable<'a> {
    table: &'a [u8],
}

pub struct ELFParser<'a> {
    object: &'a Object,
    header: elf64::ELFHeader,
    section_names: HashMap<usize, String>,
    string_table: Option<StringTable<'a>>,
    section_name_offset_mapping: HashMap<String, usize>,
}

impl<'a> ELFParser<'a> {
    pub fn new_parser(obj: &'a Object) -> MidasSysResultDynamic<ELFParser<'a>> {
        let header = elf64::ELFHeader::from(&obj.data[..])?;
        Ok(ELFParser {
            object: obj,
            header,
            section_names: HashMap::new(),
            string_table: None,
            section_name_offset_mapping: HashMap::new(),
        })
    }

    pub fn cache_section_names(&mut self) {
        let mut section_headers = self
            .get_section_headers()
            .expect("expected section header data to be intepretable");
        let string_table_file_offset = section_headers
            .get(self.header.section_header_string_index as usize)
            .unwrap()
            .offset;
        section_headers.sort_by(|a, b| a.string_table_index.cmp(&b.string_table_index));

        let string_table_data = &self.object.data[string_table_file_offset as usize..];
        debug_assert_eq!(string_table_data[0], 0);
        self.section_names.reserve(section_headers.len());

        for sh in section_headers.iter() {
            let mut name = String::with_capacity(100);
            let idx = sh.string_table_index;
            let name_ = string_table_data
                .iter()
                .skip(idx as usize)
                .take_while(|c| **c != 0)
                .map(|&c| c as char)
                .collect();

            name.shrink_to_fit();
            self.section_names.insert(idx as usize, name_);
        }

        for (k, v) in self.section_names.iter() {
            println!("Key: {} => {}", k, v);
        }
    }

    pub fn get_section_header_name(
        &'a self,
        section_header: &sectionheader::SectionHeader,
    ) -> MidasSysResultDynamic<&'a str> {
        if self.section_names.len() == 0 {
            let idx = section_header.string_table_index as usize;
            let bytes = self.string_table_data()?;
            let str_term = bytes.iter().skip(idx).position(|&b| b == 0);
            if let Some(pos) = str_term {
                std::str::from_utf8(&bytes[idx..idx + pos]).map_err(|err| err.to_string())
            } else {
                Err("Could not find string null terminator in string table".to_string())
            }
        } else {
            self.section_names
                .get(&(section_header.string_table_index as usize))
                .map(|s| s.as_ref())
                .ok_or("Error retrieving cached string table name".into())
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
        let section_headers = self.get_section_headers().unwrap();
        let symbol_table_data = self.string_table_data().unwrap();
        for (index, sh) in section_headers.iter().enumerate() {
            println!(
                "Section Header Entry #{}: {}",
                index,
                self.get_section_header_name(sh).unwrap()
            );
            println!("{:#?}", sh);
        }
    }

    pub fn get_section_headers(&self) -> MidasSysResultDynamic<Vec<sectionheader::SectionHeader>> {
        let section_header_size = self.header.section_header_entry_size as usize;
        let sh_offs = self.header.section_header_offset as usize;
        let mut section_headers = vec![];
        for x in 0..self.header.section_header_entries as usize {
            let stride = x * section_header_size;
            let start = sh_offs + stride;
            let end = sh_offs + stride + section_header_size;
            let slice = &self.object.data[start..end];
            unsafe {
                let ptr = slice.as_ptr() as *const libc::Elf64_Shdr;
                let section_header = sectionheader::SectionHeader::from_libc_repr(&*ptr);
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
        let section_headers = self.get_section_headers()?;
        let string_table_file_offset = section_headers
            .get(self.header.section_header_string_index as usize)
            .unwrap()
            .offset;
        Ok(&self.object.data[string_table_file_offset as usize..])
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
