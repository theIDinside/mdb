#[cfg(target_arch = "x86_64")]
pub struct ELFHeader {
    ident: [u8; super::EI_NIDENT],
    object_type: u16,
    machine: u16,
    version: u32,
    entry: u64,
    program_header_offset: u64,
    section_header_offset: u64,
    flags: u32,
    elf_header_size: u16,
    program_header_entry_size: u16,
    program_header_entry_count: u16,
    section_header_entry_size: u16,
    section_header_entry_count: u16,
    section_header_string_index: u16,
}

#[cfg(target_arch = "x86_64")]
pub struct ProgramHeader {
    segment_type: u32,
    flags: u32,
    offset: u64,
    virtual_addr: u64,
    physical_addr: u64,
    file_size: u64,
    memory_size: u64,
    align: u64,
}

#[cfg(target_arch = "x86_64")]
pub struct SectionHeader {
    name_index: u32, // indexes into the section header string table
    section_type: u32,
    flags: u64,
    address: u64, // address to the first byte, where this section appears in the memory image of a process
    offset: u64,
    size: u64,
    link: u32,
    info: u32,
    address_alignment: u64,
    entry_size: u64,
}

#[cfg(target_arch = "x86_64")]
pub struct SymbolTable {
    string_table_name_index: u32,
    symbol_type: u8,
    symbol_visibility: u8,
    section_header_table_index: u16,
    value: u64,
    size: u64,
}

#[cfg(target_arch = "x86_64")]
pub struct RelocationEntry {}
