pub struct SectionHeader {
    pub string_table_index: u32,
    pub segment_type: SectionType,
    pub flags: u64,
    pub address: u64,
    pub offset: u64,
    pub size: u64,
    pub section_header_table_index_link: u32,
    pub auxiliary_info: u32,
    pub address_align: u64,
    pub entry_size: u64,
}

impl SectionHeader {
    pub fn flag_is_set(&self, flag: SectionFlags) -> bool {
        self.flags & (unsafe { *(&flag as *const SectionFlags as *const u64) }) != 0
    }

    pub fn from_libc_repr(repr: &libc::Elf64_Shdr) -> SectionHeader {
        SectionHeader {
            string_table_index: repr.sh_name,
            segment_type: unsafe { std::mem::transmute(repr.sh_type) },
            flags: repr.sh_flags,
            address: repr.sh_addr,
            offset: repr.sh_offset,
            size: repr.sh_size,
            section_header_table_index_link: repr.sh_link,
            auxiliary_info: repr.sh_info,
            address_align: repr.sh_addralign,
            entry_size: repr.sh_entsize,
        }
    }
}

impl std::fmt::Debug for SectionHeader {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SectionHeader")
            .field("String Table Index", &self.string_table_index)
            .field("Section Type", &self.segment_type)
            .field("Flags", &self.flags)
            .field("Address", &format!("0x{:X}", &self.address))
            .field("Object file offset", &format!("0x{:X}", &self.offset))
            .field("Size", &self.size)
            .field(
                "Section Header Table Index",
                &self.section_header_table_index_link,
            )
            .field("Info", &self.auxiliary_info)
            .field("Address alignment", &self.address_align)
            .field("Section entry size", &self.entry_size)
            .finish()
    }
}

#[repr(u32)]
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum SectionType {
    Unused = 0,                         /* Section header table entry unused */
    ProgramData = 1,                    /* Program data */
    SymbolTable = 2,                    /* Symbol table */
    StringTable = 3,                    /* String table */
    RelocationEntriesWithAddends = 4,   /* Relocation entries with addends */
    SymbolHashTable = 5,                /* Symbol hash table */
    DynamicLinkInfo = 6,                /* Dynamic linking information */
    Auxiliary = 7,                      /* Notes */
    ProgramSpaceWithNoData = 8,         /* Program space with no data (bss) */
    RelocationEntriesNoAddends = 9,     /* Relocation entries, no addends */
    Reserved = 10,                      /* Reserved */
    DynamicLinkerSymbolTable = 11,      /* Dynamic linker symbol table */
    ArrayOfConstructors = 14,           /* Array of constructors */
    ArrayOfDestructors = 15,            /* Array of destructors */
    ArrayOfPreConstructors = 16,        /* Array of pre-constructors */
    SectionGroup = 17,                  /* Section group */
    SymbolTableSectionHeaderIndex = 18, /* Extended section indeces (SYMTAB_SHNDX) */
    DefinedTypeCount = 19,              /* Number of defined types.  */
    OsSpecificStart = 0x60000000,       /* Start OS-specific.  */
    ObjectAttributes = 0x6ffffff5,      /* Object attributes.  */
    GNUHashTable = 0x6ffffff6,          /* GNU-style hash table.  */
    GNUPreLinkLibraryList = 0x6ffffff7, /* Prelink library list */
    DSOCheckSum = 0x6ffffff8,           /* Checksum for DSO content.  */
    /* Sun-specific low bound.  */
    SUNW_move = 0x6ffffffa,
    SUNW_COMDAT = 0x6ffffffb,
    SUNW_syminfo = 0x6ffffffc,
    GNUVersionDefinition = 0x6ffffffd,   /* Version definition section.  */
    GNUVersionNeedsSection = 0x6ffffffe, /* Version needs section.  */
    GNUVersionSymbolTable = 0x6fffffff,  /* Version symbol table.  */
    /* Sun-specific high bound.  */
    /* End OS-specific type */
    LOPROC = 0x70000000, /* Start of processor-specific */
    HIPROC = 0x7fffffff, /* End of processor-specific */
    LOUSER = 0x80000000, /* Start of application-specific */
    HIUSER = 0x8fffffff, /* End of application-specific */
    Invalid,
}

#[repr(u64)]
#[derive(Debug)]
pub enum SectionFlags {
    Write = (1 << 0),                       /* Writable */
    Alloc = (1 << 1),                       /* Occupies memory during execution */
    Excutable = (1 << 2),                   /* Executable */
    Mergable = (1 << 4),                    /* Might be merged */
    Strings = (1 << 5),                     /* Contains nul-terminated strings */
    ContainsSHTIndex = (1 << 6),            /* `sh_info' contains SHT index */
    PreserveOrder = (1 << 7),               /* Preserve order after combining */
    NonConformant = (1 << 8),               /* Non-standard OS specific handling required */
    MemberOfGroup = (1 << 9),               /* Section is member of a group.  */
    ThreadLocalStorage = (1 << 10),         /* Section hold thread-local data.  */
    CompressedData = (1 << 11),             /* Section with compressed data. */
    OsSpecific = 0x0ff00000,                /* OS-specific.  */
    ProcessorSpecific = 0xf0000000,         /* Processor-specific */
    SpecialOrderingRequirement = (1 << 30), /* Special ordering requirement (Solaris).  */
    Exclude = (1 << 31),                    /* Section is excluded unless referenced or allocated (Solaris).*/
    Invalid,
}
