// System modules
use std::rc::Rc;
// Project / crate modules
use crate::elf::{Object, ParsedELF};

pub struct DebugInfo {
    object: Rc<Object>,
    pub elf: ParsedELF,
}

impl DebugInfo {
    pub fn new<S: AsRef<std::path::Path>>(executable_path: S) -> crate::MidasSysResult<DebugInfo> {
        // handle to binary data, is now behind a reference counted pointer.
        let object = crate::elf::load_object(executable_path.as_ref())?;
        let elf = ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of ddump_analysis");
        Ok(DebugInfo { object, elf })
    }

    pub fn new2<S>(executable_path: S) -> crate::MidasSysResult<DebugInfo>
    where
        S: AsRef<std::path::Path>,
    {
        // handle to binary data, is now behind a reference counted pointer.
        let object = crate::elf::load_object(executable_path.as_ref())?;
        let elf = ParsedELF::parse_elf(object.clone()).expect("failed to parse ELF of ddump_analysis");
        Ok(DebugInfo { object, elf })
    }
}
