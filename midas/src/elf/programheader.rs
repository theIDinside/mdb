use nixwrap::MidasSysResultDynamic;
#[derive(Debug)]
#[repr(u32)]
pub enum Permission {
    Executable = 0b001,
    WriteOnly = 0b010,
    ReadOnly = 0b100,
    ExecuteWrite = 0b011,
    ExecuteRead = 0b101,
    ReadWrite = 0b110,
    ExecuteReadWrite = 0b111,
    Invalid,
}

pub struct ProgramHeader {
    pub ph_type: Type,
    pub flags: Permission,
    pub file_offset: u64,
    pub virtual_address: u64,
    pub physical_address: u64,
    pub file_size: u64,
    pub memory_size: u64,
    pub align: u64,
    header_object_file_offset: usize,
}

impl std::fmt::Debug for ProgramHeader {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", format_args!("Program Header Type: {:?}\nPermissions: {:?}\nOffset in file: {}\nVirtual address: 0x{:X}\nPhysical address: 0x{:X}\nSize in file: {} bytes\nSize in memory: {} bytes\nAlignment: {} bytes\n",  &self.ph_type, &self.flags, &self.file_offset, &self.virtual_address, &self.physical_address, &self.file_size, &self.memory_size, &self.align))
    }
}

impl ProgramHeader {
    // todo(simon): should return MidasSysResult, i.e. a non-allocating error
    pub fn from_libc_repr(
        repr: &libc::Elf64_Phdr,
        object_file_byte_offset: usize,
    ) -> MidasSysResultDynamic<ProgramHeader> {
        Ok(ProgramHeader {
            ph_type: Type::from(repr.p_type).unwrap(),
            flags: unsafe { std::mem::transmute(repr.p_flags) },
            file_offset: repr.p_offset,
            virtual_address: repr.p_vaddr,
            physical_address: repr.p_paddr,
            file_size: repr.p_filesz,
            memory_size: repr.p_memsz,
            align: repr.p_align,
            header_object_file_offset: object_file_byte_offset,
        })
    }

    pub fn header_object_file_offset(&self) -> usize {
        self.header_object_file_offset
    }
}

#[allow(non_camel_case_types)]
#[repr(u32)]
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Type {
    Unused = 0,                              /* Program header table entry unused */
    Loadable = 1,                            /* Loadable program segment */
    DynamicLinkInfo = 2,                     /* Dynamic linking information */
    ProgramInterpreter = 3,                  /* Program interpreter */
    Note = 4,                                /* Auxiliary information */
    Reserved = 5,                            /* Reserved */
    HeaderTable = 6,                         /* Entry for header table itself */
    ThreadLocalStorage = 7,                  /* Thread-local storage segment */
    DefinedTypeCount = 8,                    /* Number of defined types */
    OSSpecific = 0x60000000,                 /* Start of OS-specific */
    GNU_EH_FRAME = 0x6474e550,               /* GCC .eh_frame_hdr segment */
    GNUStackExecutability = 0x6474e551,      /* Indicates stack executability */
    GNUReadOnlyAfterRelocation = 0x6474e552, /* Read-only after relocation */
    GNU_PROPERTRY = 0x6474e553, // N.B. - does not exist as a macro definition in /usr/include/elf.h; but is something new to the linux kernel / ABI related stuff, according to; https://raw.githubusercontent.com/wiki/hjl-tools/linux-abi/linux-abi-draft.pdf, page 19
    SunSpecificBSS = 0x6ffffffa, /* Sun Specific segment */
    SUNWSTACK = 0x6ffffffb,     /* Stack segment */
    EndOfOSSpecific = 0x6fffffff, /* End of OS-specific */
    StartProcessorSpecific = 0x70000000, /* Start of processor-specific */
    EndProcessorSpecific = 0x7fffffff, /* End of processor-specific */
}

impl Type {
    pub fn from(value: u32) -> MidasSysResultDynamic<Type> {
        match value {
            0 => Ok(Self::Unused),
            1 => Ok(Self::Loadable),
            2 => Ok(Self::DynamicLinkInfo),
            3 => Ok(Self::ProgramInterpreter),
            4 => Ok(Self::Note),
            5 => Ok(Self::Reserved),
            6 => Ok(Self::HeaderTable),
            7 => Ok(Self::ThreadLocalStorage),
            8 => Ok(Self::DefinedTypeCount),
            0x60000000 => Ok(Self::OSSpecific),
            0x6474e550 => Ok(Self::GNU_EH_FRAME),
            0x6474e551 => Ok(Self::GNUStackExecutability),
            0x6474e552 => Ok(Self::GNUReadOnlyAfterRelocation),
            0x6474e553 => Ok(Self::GNU_PROPERTRY),
            0x6ffffffa => Ok(Self::SunSpecificBSS),
            0x6ffffffb => Ok(Self::SUNWSTACK),
            0x6fffffff => Ok(Self::EndOfOSSpecific),
            0x70000000 => Ok(Self::StartProcessorSpecific),
            0x7fffffff => Ok(Self::EndProcessorSpecific),
            _ => Err("Illegal or undefined Program Header Type".into()),
        }
    }
}
