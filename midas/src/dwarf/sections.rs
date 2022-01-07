use crate::MidasError;

/// An identifier for a DWARF section.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Ord, PartialOrd, Hash)]
pub enum Section {
    /// The `.debug_abbrev` section.
    DebugAbbrev,
    /// The `.debug_addr` section.
    DebugAddr,
    /// The `.debug_aranges` section.
    DebugAranges,
    /// The `.debug_cu_index` section.
    DebugCuIndex,
    /// The `.debug_frame` section.
    DebugFrame,
    /// The `.eh_frame` section.
    EhFrame,
    /// The `.eh_frame_hdr` section.
    EhFrameHeader,
    /// The `.debug_info` section.
    DebugInfo,
    /// The `.debug_line` section.
    DebugLine,
    /// The `.debug_line_str` section.
    DebugLineStr,
    /// The `.debug_loc` section.
    DebugLoc,
    /// The `.debug_loclists` section.
    DebugLocLists,
    /// The `.debug_macinfo` section.
    DebugMacinfo,
    /// The `.debug_macro` section.
    DebugMacro,
    /// The `.debug_pubnames` section.
    DebugPubNames,
    /// The `.debug_pubtypes` section.
    DebugPubTypes,
    /// The `.debug_ranges` section.
    DebugRanges,
    /// The `.debug_rnglists` section.
    DebugRngLists,
    /// The `.debug_str` section.
    DebugStr,
    /// The `.debug_str_offsets` section.
    DebugStrOffsets,
    /// The `.debug_tu_index` section.
    DebugTuIndex,
    /// The `.debug_types` section.
    DebugTypes,
}

impl TryFrom<&str> for Section {
    type Error = MidasError;
    fn try_from(value: &str) -> Result<Self, Self::Error> {
        match value {
            ".debug_abbrev" => Ok(Self::DebugAbbrev),
            ".debug_addr" => Ok(Self::DebugAddr),
            ".debug_aranges" => Ok(Self::DebugAranges),
            ".debug_cu_index" => Ok(Self::DebugCuIndex),
            ".debug_frame" => Ok(Self::DebugFrame),
            ".eh_frame" => Ok(Self::EhFrame),
            ".eh_frame_hdr" => Ok(Self::EhFrameHeader),
            ".debug_info" => Ok(Self::DebugInfo),
            ".debug_line" => Ok(Self::DebugLine),
            ".debug_line_str" => Ok(Self::DebugLineStr),
            ".debug_loc" => Ok(Self::DebugLoc),
            ".debug_loclists" => Ok(Self::DebugLocLists),
            ".debug_macinfo" => Ok(Self::DebugMacinfo),
            ".debug_macro" => Ok(Self::DebugMacro),
            ".debug_pubnames" => Ok(Self::DebugPubNames),
            ".debug_pubtypes" => Ok(Self::DebugPubTypes),
            ".debug_ranges" => Ok(Self::DebugRanges),
            ".debug_rnglists" => Ok(Self::DebugRngLists),
            ".debug_str" => Ok(Self::DebugStr),
            ".debug_str_offsets" => Ok(Self::DebugStrOffsets),
            ".debug_tu_index" => Ok(Self::DebugTuIndex),
            ".debug_types" => Ok(Self::DebugTypes),
            _ => Err(MidasError::DwarfSectionNotRecognized),
        }
    }
}
