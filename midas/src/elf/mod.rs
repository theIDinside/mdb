#![allow(unused, non_camel_case_types)]
mod elf32;
mod elf64;
mod programheader;
mod section;
pub mod symbol;
use super::dwarf;

use std::{collections::HashMap, io::Read};

#[cfg(target_arch = "x86")]
pub use elf32::*;

#[cfg(target_arch = "x86_64")]
pub use elf64::*;
use nixwrap::MidasSysResultDynamic;

use crate::{
    bytereader::{self, NonConsumingReader},
    utils::midas_err,
    ELFSection, MidasError, MidasSysResult,
};

use self::{programheader::ProgramHeader, symbol::SymbolTable};

pub struct Object {
    pub data: Vec<u8>,
    pub bytes_read: usize,
}

pub struct StringTable<'a> {
    table: &'a [u8],
}
pub fn cheat<'a>(ptr_and_len: (*const u8, usize)) -> &'a [u8] {
    unsafe {
        let (ptr, len) = ptr_and_len;
        std::slice::from_raw_parts(ptr, len)
    }
}

#[derive(Default)]
pub struct ParsedSections {
    sections: HashMap<dwarf::Section, (*const u8, usize)>,
}

impl ParsedSections {
    pub fn insert(&mut self, section: dwarf::Section, section_data: &[u8]) {
        self.sections
            .insert(section, (section_data.as_ptr(), section_data.len()));
    }

    pub fn get(&self, section: dwarf::Section) -> Option<&[u8]> {
        let opt = self.sections.get(&section);
        if let Some(ptr_len) = opt {
            Some(cheat(*ptr_len))
        } else {
            None
        }
    }
}

pub struct ParsedELF<'object> {
    // we take a reference counted pointer to the loaded object, but all operations, circumvent the access of it. We do it once and keep
    // the Rc around, to make sure it never dies.
    object: std::rc::Rc<Object>,
    header: elf64::ELFHeader,
    dwarf_sections: ParsedSections,
    sections: HashMap<String, (section::SectionHeader, section::Section)>,
    pub symbol_table: SymbolTable<'object>,
}

impl<'object> ParsedELF<'object> {
    pub fn parse_elf(obj: &'object std::rc::Rc<Object>) -> MidasSysResult<ParsedELF<'object>> {
        let header = elf64::ELFHeader::from(&obj.data[..])?;
        let obj_ref = std::rc::Rc::as_ref(&obj);
        let mut sections = HashMap::new();
        let section_headers = Self::parse_section_headers(&header, obj_ref)?;

        let mut dwarf_sections = ParsedSections::default();

        let section_header_string_table_file_offset = section_headers
            .get(header.section_header_string_index as usize)
            .unwrap()
            .section_data_offset;

        let section_header_string_table = &obj_ref.data[section_header_string_table_file_offset as usize..];
        let sh_ent_sz = header.section_header_entry_size as usize;
        for (index, sh) in section_headers.into_iter().enumerate() {
            let idx = sh.string_table_index as usize;
            let str_term = section_header_string_table
                .iter()
                .skip(idx)
                .position(|&b| b == 0)
                .expect("string table entry null terminator expected.");
            let section_name = std::str::from_utf8(&section_header_string_table[idx..idx + str_term])?;
            let section_data_in_obj_f = &obj_ref.data[sh.address_range()];
            if let Ok(section_id) = dwarf::Section::try_from(section_name) {
                dwarf_sections.insert(section_id, section_data_in_obj_f);
            }
            let section = section::Section::from_object_file(index, &obj_ref, &sh);
            sections.insert(section_name.to_owned(), (sh, section));
        }

        let (strtab_header, strtab_sec) = sections.get(".strtab").as_ref().unwrap();
        let (symtab_header, symtab_sec) = sections.get(".symtab").as_ref().unwrap();

        let mut reader = bytereader::ConsumeReader::wrap(&obj_ref.data[symtab_header.address_range()]);
        let st_reader = NonConsumingReader::new(&obj_ref.data[strtab_header.address_range()]);
        let symbol_table = SymbolTable::parse_symbol_table(&symtab_header, reader, &st_reader)?;

        // todo(simon): this is hacky as shit. I've done this, because I had to figure out how dwarf elf etc actually works first
        // when it's functioning, this *will* be refactored, so that we don't create unnecessary hashmaps
        let pe = ParsedELF {
            object: obj.clone(),
            header,
            dwarf_sections,
            sections,
            symbol_table,
        };
        Ok(pe)
    }

    pub fn get_section_data(&'object self, name: &str) -> Option<&'object [u8]> {
        self.sections.get(name).map(|(header, sec)| sec.data())
    }

    // a bit more optimized search, we don't have to hash a string first
    pub fn get_dwarf_section_data(&self, dwarf_section: dwarf::Section) -> Option<&[u8]> {
        self.dwarf_sections.get(dwarf_section)
    }

    pub fn get_section_header_name(
        &'object self,
        section_header: &section::SectionHeader,
    ) -> MidasSysResultDynamic<&'object str> {
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

    pub fn get_raw_segment_headers_of(&'object self, segment_type: programheader::Type) -> Vec<&'object [u8]> {
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

        let mut ref_vec: Vec<(&String, &(section::SectionHeader, section::Section))> = self.sections.iter().collect();
        ref_vec.sort_by(|(_, (_, asec)), (_, (_, bsec))| asec.section_index.cmp(&bsec.section_index));

        for (name, (hdr, sec)) in ref_vec {
            println!("Section Header Entry #{}: {}", sec.section_index, name);
            println!("{:#?}", hdr);
        }
    }

    pub fn parse_section_headers(
        elf_header: &elf64::ELFHeader,
        object: &Object,
    ) -> MidasSysResult<Vec<section::SectionHeader>> {
        let section_header_size = elf_header.section_header_entry_size as usize;
        let start = elf_header.section_header_offset as usize;

        let end = start + (section_header_size * elf_header.sections_count());

        let mut reader = crate::bytereader::ConsumeReader::wrap(&object.data[start..end]);
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

    pub fn get_interpreter(&'object self) -> MidasSysResultDynamic<&'object str> {
        let interpreter_header = self
            .get_program_segment_headers_of(programheader::Type::ProgramInterpreter)
            .ok_or("Could not find interpreter headers".to_string())?;
        let ph = unsafe { interpreter_header.get(0).unwrap_unchecked() };

        std::str::from_utf8(&self.object.data[ph.file_offset as usize..(ph.file_offset + ph.file_size) as usize])
            .map_err(|err| err.to_string())
    }

    pub fn string_table_data(&self) -> MidasSysResultDynamic<&[u8]> {
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
            .get(dwarf_section)
            .ok_or(crate::MidasError::DwarfSectionNotRecognized)
    }

    pub fn parse_symbol_table(&'object self) -> MidasSysResult<SymbolTable<'object>> {
        let (header, section) = self
            .sections
            .get(".symtab")
            .ok_or(MidasError::SectionNotFound(ELFSection::SymbolTable))?;

        let mut reader = bytereader::ConsumeReader::wrap(section.data());

        let st_reader = NonConsumingReader::new(self.string_table_data().unwrap());
        let symtab = SymbolTable::parse_symbol_table(&header, reader, &st_reader)?;
        Ok(symtab)
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

pub fn load_object(path: &std::path::Path) -> MidasSysResultDynamic<std::rc::Rc<Object>> {
    let mut buf = vec![];
    let mut f = std::fs::OpenOptions::new()
        .read(true)
        .create_new(false)
        .open(path)
        .map_err(midas_err)?;
    let file_size = f.metadata().map_err(midas_err)?.len();
    buf.reserve(file_size as _);
    let bytes_read = f.read_to_end(&mut buf).map_err(midas_err)?;
    Ok(std::rc::Rc::new(Object::new(buf, bytes_read)))
}
