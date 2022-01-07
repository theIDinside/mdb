use super::section;
use crate::bytereader::Reader;
use std::collections::HashMap;

pub struct SymbolTable<'object> {
    no_type: HashMap<&'object str, Symbol>,
    objects: HashMap<&'object str, Symbol>,
    functions: HashMap<&'object str, Symbol>,
    sections: HashMap<&'object str, Symbol>,
    files: HashMap<&'object str, Symbol>,
}

impl<'object> SymbolTable<'object> {
    pub fn parse_symbol_table(section_header: &section::SectionHeader, reader: Reader) -> SymbolTable<'object> {
        unimplemented!("parse .symtab not implemented");
        Self {
            no_type: HashMap::new(),
            objects: HashMap::new(),
            functions: HashMap::new(),
            sections: HashMap::new(),
            files: HashMap::new(),
        }
    }
}

pub struct Symbol {
    // optimizations go boom! j/k. This allows for the value = 0, to be "seen" by us as Option<..>::None
    address: Option<std::num::NonZeroUsize>,
    visibility: u8,
    binding: Binding,
}

#[derive(Debug)]
#[repr(u8)]
pub enum Binding {
    Local = 0,
    Global = 1,
    Weak = 2,
    LOProc = 13,
    HIProc = 15,
}

#[repr(u8)]
pub enum Type {
    None = 0,
    Object = 1,
    Function = 2,
    Section = 3,
    File = 4,
    LOProc = 13,
    HIProc = 15,
}

#[inline]
pub fn parse_symbol_info(byte: u8) -> (Binding, Type) {
    let symbol_binding = byte >> 4;
    let symbol_type = byte & 0x0f;
    unsafe {
        (
            std::mem::transmute::<u8, Binding>(symbol_binding),
            std::mem::transmute::<u8, Type>(symbol_type),
        )
    }
}
