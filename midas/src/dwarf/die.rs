use crate::types::SectionPointer;

use super::compilation_unit::CompilationUnitHeader;

pub struct DIE {
    cu_header: CompilationUnitHeader,
    debug_info: SectionPointer,
    debug_abbrev: SectionPointer,
}
