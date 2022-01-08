use super::section;
use crate::bytereader::{ByteReaderManager, ConsumeReader};
use std::collections::HashMap;

pub struct SymbolTable<'object> {
    unnamed_symbols: Vec<Symbol>,
    no_type: HashMap<&'object str, Symbol>,
    objects: HashMap<&'object str, Symbol>,
    functions: HashMap<&'object str, Symbol>,
    sections: HashMap<&'object str, Symbol>,
    files: HashMap<&'object str, Symbol>,
}

impl<'object> SymbolTable<'object> {
    pub fn parse_symbol_table(
        section_header: &section::SectionHeader,
        mut symbol_table_reader: ConsumeReader<'object>,
        string_table_data_reader: &ByteReaderManager<'object>,
    ) -> crate::MidasSysResult<SymbolTable<'object>> {
        let entries = section_header.entries_count().unwrap();
        let entry_sz = section_header.entry_size as usize;

        let mut st = Self {
            unnamed_symbols: vec![],
            no_type: HashMap::new(),
            objects: HashMap::new(),
            functions: HashMap::new(),
            sections: HashMap::new(),
            files: HashMap::new(),
        };
        let mut of = 0;
        for entry_index in 0..entries {
            let slice = symbol_table_reader.read_slice(entry_sz)?;
            unsafe {
                let ptr = slice.as_ptr() as *const libc::Elf64_Sym;
                let name_index = (*ptr).st_name;
                let address = std::num::NonZeroUsize::new((*ptr).st_value as _);

                let (binding, type_) = parse_symbol_info((*ptr).st_info);
                let section_index = (*ptr).st_shndx;

                if (*ptr).st_name == 0 {
                    st.unnamed_symbols.push(Symbol::new(
                        entry_index,
                        address,
                        (*ptr).st_size as _,
                        binding,
                        section_index as _,
                    ));
                } else {
                    let name = string_table_data_reader.read_str_from((*ptr).st_name as usize)?;
                    match type_ {
                        Type::None => {
                            st.no_type.insert(
                                name,
                                Symbol::new(
                                    entry_index,
                                    address,
                                    (*ptr).st_size as _,
                                    binding,
                                    section_index as _,
                                ),
                            );
                        }
                        Type::Object => {
                            st.objects.insert(
                                name,
                                Symbol::new(
                                    entry_index,
                                    address,
                                    (*ptr).st_size as _,
                                    binding,
                                    section_index as _,
                                ),
                            );
                        }
                        Type::Function => {
                            st.functions.insert(
                                name,
                                Symbol::new(
                                    entry_index,
                                    address,
                                    (*ptr).st_size as _,
                                    binding,
                                    section_index as _,
                                ),
                            );
                        }
                        Type::Section => {
                            st.sections.insert(
                                name,
                                Symbol::new(
                                    entry_index,
                                    address,
                                    (*ptr).st_size as _,
                                    binding,
                                    section_index as _,
                                ),
                            );
                        }
                        Type::File => {
                            st.files.insert(
                                name,
                                Symbol::new(
                                    entry_index,
                                    address,
                                    (*ptr).st_size as _,
                                    binding,
                                    section_index as _,
                                ),
                            );
                        }
                        Type::LOProc => {
                            println!("error parsing type");
                        }
                        Type::HIProc => {
                            println!("error parsing type");
                        }
                        _ => println!("error parsing type!?"),
                    }
                }
            }
        }
        Ok(st)
    }

    pub fn print_unordered(&self) {
        println!("Unnamed symbols:");
        for sym in &self.unnamed_symbols {
            println!("{:?}", sym);
        }

        println!("Symbols; type = No type");
        for (name, sym) in &self.no_type {
            println!("|  {:^50}  | => [  {:?}  ]", name, sym)
        }

        println!("Symbols; type = Object");
        for (name, sym) in &self.objects {
            println!("|  {:^50}  | => [  {:?}  ]", name, sym)
        }

        println!("Symbols; type = Function");
        for (name, sym) in &self.functions {
            println!("|  {:^50}  | => [  {:?}  ]", name, sym)
        }

        println!("Symbols; type = Section");
        for (name, sym) in &self.sections {
            println!("|  {:^50}  | => [  {:?}  ]", name, sym)
        }

        println!("Symbols; type = File");
        for (name, sym) in &self.files {
            println!("|  {:^50}  | => [  {:?}  ]", name, sym)
        }
    }
}

pub struct Symbol {
    // optimizations go boom! j/k. This allows for the value = 0, to be "seen" by us as Option<..>::None
    address: Option<std::num::NonZeroUsize>,
    size: usize,
    binding: Binding,
    section_index: usize,
    entry_index: usize,
}

impl Symbol {
    pub fn new(
        entry_index: usize,
        address: Option<std::num::NonZeroUsize>,
        size: usize,
        binding: Binding,
        section_index: usize,
    ) -> Symbol {
        Symbol {
            entry_index,
            address,
            size,
            binding,
            section_index,
        }
    }
}

impl std::fmt::Debug for Symbol {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let a = std::num::NonZeroUsize::new(1);
        let b = a.unwrap();
        let idx = format!("(#{})", &self.entry_index);
        write!(
            f,
            "#{:>8} 0x{:016X?}, {:>32} bytes. {:>10?}. Index #{:>3}",
            idx,
            &self.address.map(|v| v.get()).unwrap_or(0),
            &self.size,
            &self.binding,
            &self.section_index
        )
    }
}

#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum Binding {
    Local = 0,
    Global = 1,
    Weak = 2,
    LOProc = 13,
    HIProc = 15,
}

#[derive(Debug, Clone, Copy)]
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
