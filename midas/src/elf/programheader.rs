use nixwrap::MidasSysResult;

pub struct ProgramHeader {
    pub ph_type: Type,
    pub flags: u32,
    pub offset: u64,
    pub virtual_address: u64,
    pub physical_address: u64,
    pub file_size: u64,
    pub memory_size: u64,
    pub align: u64,
}

impl Default for ProgramHeader {
    fn default() -> Self {
        Self {
            ph_type: Type::Unused,
            flags: Default::default(),
            offset: Default::default(),
            virtual_address: Default::default(),
            physical_address: Default::default(),
            file_size: Default::default(),
            memory_size: Default::default(),
            align: Default::default(),
        }
    }
}

impl ProgramHeader {
    // N.B. it's up to you, to make sure that this slice actually begins where this program header starts. If you blow it up, it's on you.
    #[allow(unused)]
    pub fn from_bytes(bytes: &[u8]) -> MidasSysResult<ProgramHeader> {
        let ph_type = Type::from(unsafe { crate::utils::unchecked::bytes_to_u32(&bytes[..4]) })?;
        let mut ph = ProgramHeader::default();
        ph.ph_type = ph_type;

        unsafe {
            // we in da danja zone
            let offset = bytes.as_ptr().offset(4);
            let mut ph_slice = crate::utils::unchecked::as_mut_bytes(&ph);
            let ph_ptr = ph_slice.as_mut_ptr();
            std::ptr::copy_nonoverlapping(
                offset,
                ph_ptr.offset(std::mem::size_of::<u32>() as _),
                56 - 4,
            );
        }
        Ok(ph)
    }
}
#[allow(non_camel_case_types)]
#[repr(u32)]
pub enum Type {
    Unused,                  /* Program header table entry unused */
    Loadable,                /* Loadable program segment */
    DynamicLinkInfo,         /* Dynamic linking information */
    ProgramInterpreter,      /* Program interpreter */
    Auxiliary,               /* Auxiliary information */
    Reserved,                /* Reserved */
    HeaderTable,             /* Entry for header table itself */
    ThreadLocalStorage,      /* Thread-local storage segment */
    DefinedTypeCount,        /* Number of defined types */
    OSSpecific,              /* Start of OS-specific */
    GNU_EH_FRAME,            /* GCC .eh_frame_hdr segment */
    StackExecutability,      /* Indicates stack executability */
    ReadOnlyAfterRelocation, /* Read-only after relocation */
    SunSpecificBSS,          /* Sun Specific segment */
    SUNWSTACK,               /* Stack segment */
    EndOfOSSpecific,         /* End of OS-specific */
    StartProcessorSpecific,  /* Start of processor-specific */
    EndProcessorSpecific,    /* End of processor-specific */
}

impl Type {
    pub fn from(value: u32) -> MidasSysResult<Type> {
        match value {
            0 => Ok(Self::Unused),
            1 => Ok(Self::Loadable),
            2 => Ok(Self::DynamicLinkInfo),
            3 => Ok(Self::ProgramInterpreter),
            4 => Ok(Self::Auxiliary),
            5 => Ok(Self::Reserved),
            6 => Ok(Self::HeaderTable),
            7 => Ok(Self::ThreadLocalStorage),
            8 => Ok(Self::DefinedTypeCount),
            0x60000000 => Ok(Self::OSSpecific),
            0x6474e550 => Ok(Self::GNU_EH_FRAME),
            0x6474e551 => Ok(Self::StackExecutability),
            0x6474e552 => Ok(Self::ReadOnlyAfterRelocation),
            0x6ffffffa => Ok(Self::SunSpecificBSS),
            0x6ffffffb => Ok(Self::SUNWSTACK),
            0x6fffffff => Ok(Self::EndOfOSSpecific),
            0x70000000 => Ok(Self::StartProcessorSpecific),
            0x7fffffff => Ok(Self::EndProcessorSpecific),
            _ => Err("Illegal or undefined Program Header Type".into()),
        }
    }
}
