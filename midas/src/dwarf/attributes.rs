#![allow(unused, non_camel_case_types)]
use crate::{bytereader, MidasError, MidasSysResult};
use std::collections::HashMap;

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct AbbreviationsTableEntry {
    pub tag: DwarfTag,
    pub attrs_list: Vec<(Attribute, AttributeForm)>,
    pub has_children: bool,
}

impl AbbreviationsTableEntry {
    pub fn new(tag: DwarfTag, attrs_list: Vec<(Attribute, AttributeForm)>, has_children: bool) -> Self {
        Self {
            tag,
            attrs_list,
            has_children,
        }
    }
}

pub struct AbbreviationsTableIterator<'a> {
    abbrev_table: &'a [u8],
    cu_iterator: CompilationUnitHeaderIterator<'a>,
}

impl<'a> AbbreviationsTableIterator<'a> {
    pub fn new(
        abbrev_table: &'a [u8],
        cu_iterator: CompilationUnitHeaderIterator<'a>,
    ) -> AbbreviationsTableIterator<'a> {
        AbbreviationsTableIterator {
            abbrev_table,
            cu_iterator,
        }
    }
}

impl<'a> Iterator for AbbreviationsTableIterator<'a> {
    type Item = HashMap<u64, AbbreviationsTableEntry>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(cu_header) = self.cu_iterator.next() {
            let offset = cu_header.abbreviation_offset;
            let attrs = parse_cu_attributes(&self.abbrev_table[offset..]);
            attrs.ok()
        } else {
            None
        }
    }
}
use super::{compilation_unit::CompilationUnitHeaderIterator, tag::DwarfTag};
type AttributeIndex = usize;

// Parses the Abbreviations table entries for the compilation unit which references `abbreviations_table_data`
pub fn parse_cu_attributes(
    abbreviations_table_data: &[u8],
) -> crate::MidasSysResult<HashMap<u64, AbbreviationsTableEntry>> {
    let mut map = HashMap::new();
    let mut reader = crate::bytereader::ConsumeReader::wrap(abbreviations_table_data);

    while reader.has_more() {
        let abbrev_code = reader.read_uleb128()?;
        if abbrev_code == 0 {
            break;
        }
        let mut attrs_list = Vec::with_capacity(6);
        let tag = reader.read_uleb128()?;
        let has_children = reader.read_u8() == 1;
        let tag = unsafe { std::mem::transmute(tag as u16) };

        'attr_list: loop {
            let attr = reader.read_uleb128()?;
            let form = reader.read_uleb128()?;
            if attr == 0 && form == 0 {
                break 'attr_list;
            }
            let (attr, form) = unsafe {
                (
                    std::mem::transmute(attr as u16),
                    std::mem::transmute(form as u8),
                )
            };
            attrs_list.push((attr, form));
        }
        attrs_list.shrink_to_fit();

        map.insert(
            abbrev_code,
            AbbreviationsTableEntry::new(tag, attrs_list, has_children),
        );
    }
    Ok(map)
}

// this is marked as unsafe due to this; data *must* be correct. Thus, the hurdle of wrapping this in unsafe, ensures I never forget this.
pub unsafe fn parse_attribute_list(data: &[u8]) -> MidasSysResult<(u64, AbbreviationsTableEntry)> {
    let mut reader = crate::bytereader::ConsumeReader::wrap(data);

    let abbrev_code = reader.read_uleb128()?;
    if abbrev_code == 0 {
        panic!("failed to parse attribute info");
    }
    let mut attrs_list = Vec::with_capacity(6);
    let tag = reader.read_uleb128()?;
    let has_children = reader.read_u8() == 1;
    let tag = unsafe { std::mem::transmute(tag as u16) };

    'attr_list: loop {
        let attr = reader.read_uleb128()?;
        let form = reader.read_uleb128()?;
        if attr == 0 && form == 0 {
            break 'attr_list;
        }
        let (attr, form) = unsafe {
            (
                std::mem::transmute(attr as u16),
                std::mem::transmute(form as u8),
            )
        };
        attrs_list.push((attr, form));
    }
    Ok((
        abbrev_code,
        AbbreviationsTableEntry::new(tag, attrs_list, has_children),
    ))
}

#[derive(Debug)]
pub struct ParsedAttribute {
    pub attribute: Attribute,
    pub value: AttributeValue,
}

impl ParsedAttribute {
    pub fn new(attribute: Attribute, value: AttributeValue) -> ParsedAttribute {
        ParsedAttribute { attribute, value }
    }
}
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum AttributeValue {
    Address(usize),
    Block(Vec<u8>),
    Data1(u8),
    Data2(u16),
    Data4(u32),
    Data8(u64),
    SData(i64),
    UData(u64),
    // todo(simon): we might not want to copy around data like this... could be potentially a de-throttling situation. We'll see
    Expression(Vec<u8>),
    Flag(bool),
    SectionOffset(usize), // we convert _all_ offsets to usize
    DebugAddress(usize),
    DebugAddressIndex(usize),
    CompilationUnitOffset(usize),
    DebugInfoOffset(usize),
    DebugInfoSupplementaryOffset(usize),
    DebugLineOffset(usize),
    LocationListsOffset(usize),
    DebugLocListsOffset(usize),
    DebugLocListsIndex(usize),
    DebugMacroInfoOffset(usize),
    DebugMacroOffset(usize),
    DebugRangesOffset(usize),
    DebugRangesListsOffset(usize),
    DebugRangesListIndex(usize),
    DebugTypesSignature(usize),
    DebugStrOffset(usize),
    DebugStrSupplementaryOffset(usize),
    DebugStrOffsetsOffset(usize),
    DebugStrOffsetsIndex(usize),
    DebugLineStrOffset(usize),
    String(String),
    AttributeEncoding(u8),
    DecimalSign(u8),
    /// The value of a `DW_AT_endianity` attribute.
    Endianity(u8),
    /// The value of a `DW_AT_accessibility` attribute.
    Accessibility(u8),
    /// The value of a `DW_AT_visibility` attribute.
    Visibility(u8),
    /// The value of a `DW_AT_virtuality` attribute.
    Virtuality(u8),
    /// The value of a `DW_AT_language` attribute.
    Language(u16),
    /// The value of a `DW_AT_address_class` attribute.
    AddressClass(usize),
    /// The value of a `DW_AT_identifier_case` attribute.
    IdentifierCase(u8),
    /// The value of a `DW_AT_calling_convention` attribute.
    CallingConvention(u8),
    /// The value of a `DW_AT_inline` attribute.
    Inline(u8),
    /// The value of a `DW_AT_ordering` attribute.
    Ordering(u8),
    /// An index into the filename entries from the line number information
    /// table for the compilation unit containing this value.
    FileIndex(u64),
    /// An implementation-defined identifier uniquely identifying a compilation
    /// unit.
    DwoId(u64),
}

pub fn parse_attribute(
    reader: &mut bytereader::ConsumeReader,
    encoding: super::Encoding,
    (atname, atform): (Attribute, AttributeForm),
) -> ParsedAttribute {
    use AttributeValue as AV;
    use ParsedAttribute as PA;
    let mut form = atform;
    'possible_indirection: loop {
        match form {
            AttributeForm::DW_FORM_addr => match encoding.pointer_width {
                1 => return PA::new(atname, AV::Address(reader.read_u8() as _)),
                2 => return PA::new(atname, AV::Address(reader.read_u16() as usize)),
                4 => return PA::new(atname, AV::Address(reader.read_u32() as usize)),
                8 => return PA::new(atname, AV::Address(reader.read_u64() as usize)),
                _ => panic!("invalid pointer width"),
            },
            AttributeForm::Reserved => todo!(),
            AttributeForm::DW_FORM_block2 => {
                let len = reader.read_u16() as usize;
                let data: Vec<u8> = reader.clone_slice(len).expect("failed to read BLOCK data");
                return PA::new(atname, AV::Block(data));
            }
            AttributeForm::DW_FORM_block4 => {
                let len = reader.read_u32() as usize;
                let data: Vec<u8> = reader.clone_slice(len).expect("failed to read BLOCK data");
                return PA::new(atname, AV::Block(data));
            }
            AttributeForm::DW_FORM_data2 => {
                return PA::new(atname, AV::Data2(reader.read_u16()));
            }
            AttributeForm::DW_FORM_data4 => {
                return PA::new(atname, AV::Data4(reader.read_u32()));
            }
            AttributeForm::DW_FORM_data8 => {
                return PA::new(atname, AV::Data8(reader.read_u64()));
            }
            AttributeForm::DW_FORM_string => {
                let string = reader
                    .read_str()
                    .expect("failed to read null-terminated string")
                    .to_owned();
                reader.read_u8();
                return PA::new(atname, AV::String(string));
            }
            AttributeForm::DW_FORM_block => {
                let uleb = reader.read_uleb128().expect("failed to read uleb128");
                let data = reader
                    .clone_slice(uleb as _)
                    .expect("failed to read block data");
                return PA::new(atname, AV::Block(data));
            }
            AttributeForm::DW_FORM_block1 => {
                let len = reader.read_u8();
                let data: Vec<u8> = reader
                    .clone_slice(len as _)
                    .expect("failed to read BLOCK data");
                return PA::new(atname, AV::Block(data));
            }
            AttributeForm::DW_FORM_data1 => return PA::new(atname, AV::Data1(reader.read_u8())),
            AttributeForm::DW_FORM_flag => {
                return PA::new(atname, AV::Flag(reader.read_u8() != 0));
            }
            AttributeForm::DW_FORM_sdata => {
                return PA::new(
                    atname,
                    AV::SData(reader.read_ileb128().expect("failed to read ileb128")),
                );
            }
            AttributeForm::DW_FORM_strp => match encoding.format {
                crate::dwarf::Format::DWARF32 => {
                    let offset = reader.read_u32();
                    return PA::new(atname, AV::DebugStrOffset(offset as _));
                }
                crate::dwarf::Format::DWARF64 => {
                    let offset = reader.read_u64();
                    return PA::new(atname, AV::DebugStrOffset(offset as _));
                }
            },
            AttributeForm::DW_FORM_udata => {
                return PA::new(
                    atname,
                    AV::UData(reader.read_uleb128().expect("failed to read ileb128")),
                );
            }
            AttributeForm::DW_FORM_ref_addr => {
                // This is an offset, but DWARF version 2 specifies that DW_FORM_ref_addr
                // has the same size as an address on the target system.  This was changed
                // in DWARF version 3.
                let offset = if encoding.version == 2 {
                    match encoding.pointer_width {
                        1 => reader.read_u8() as usize,
                        2 => reader.read_u16() as usize,
                        4 => reader.read_u32() as usize,
                        8 => reader.read_u64() as usize,
                        _ => panic!("invalid pointer width"),
                    }
                } else {
                    match encoding.format {
                        crate::dwarf::Format::DWARF32 => reader.read_u32() as usize,
                        crate::dwarf::Format::DWARF64 => reader.read_u64() as usize,
                    }
                };
                return PA::new(atname, AV::DebugInfoOffset(offset));
            }
            AttributeForm::DW_FORM_ref1 => {
                let offset = reader.read_u8();
                return PA::new(atname, AV::CompilationUnitOffset(offset as _));
            }
            AttributeForm::DW_FORM_ref2 => {
                let offset = reader.read_u16() as usize;
                return PA::new(atname, AV::CompilationUnitOffset(offset));
            }
            AttributeForm::DW_FORM_ref4 => {
                let offset = reader.read_u32() as usize;
                return PA::new(atname, AV::CompilationUnitOffset(offset));
            }
            AttributeForm::DW_FORM_ref8 => {
                let offset = reader.read_u64() as usize;
                return PA::new(atname, AV::CompilationUnitOffset(offset));
            }
            AttributeForm::DW_FORM_ref_udata => {
                let offset = reader.read_uleb128().expect("failed to read uleb128") as usize;
                return PA::new(atname, AV::CompilationUnitOffset(offset));
            }
            AttributeForm::DW_FORM_indirect => todo!("DW_FORM_indirect not handled yet"),
            AttributeForm::DW_FORM_sec_offset => match encoding.format {
                crate::dwarf::Format::DWARF32 => return PA::new(atname, AV::SectionOffset(reader.read_u32() as usize)),
                crate::dwarf::Format::DWARF64 => return PA::new(atname, AV::SectionOffset(reader.read_u64() as usize)),
            },
            AttributeForm::DW_FORM_exprloc => {
                let instruction_bytestream_len = reader
                    .read_uleb128()
                    .expect("failed to get length of DWARF expression");
                let instructions = reader
                    .clone_slice(instruction_bytestream_len as _)
                    .expect("failed to read DWARF expression instructions");
                return PA::new(atname, AV::Expression(instructions));
            }
            AttributeForm::DW_FORM_flag_present => return PA::new(atname, AV::Flag(true)),
            AttributeForm::DW_FORM_strx => todo!("DWARF5 version of attribute form not handled yet (strx)"),
            AttributeForm::DW_FORM_addrx => todo!("DWARF5 version of attribute form not handled yet (addrx)"),
            AttributeForm::DW_FORM_ref_sup4 => todo!("DWARF5 version of attribute form not handled yet (ref_sup4)"),
            AttributeForm::DW_FORM_strp_sup => todo!("DWARF5 version of attribute form not handled yet (strp_sup)"),
            AttributeForm::DW_FORM_data16 => todo!("DWARF5 version of attribute form not handled yet (data16)"),
            AttributeForm::DW_FORM_line_strp => todo!("DWARF5 version of attribute form not handled yet (line_strp)"),
            AttributeForm::DW_FORM_ref_sig8 => {
                let type_signature = reader.read_u64();
                return PA::new(atname, AV::DebugTypesSignature(type_signature as usize));
            }
            AttributeForm::DW_FORM_implicit_const => {
                todo!("DWARF5 version of attribute form not handled yet (implicit_const)")
            }
            AttributeForm::DW_FORM_loclistx => todo!("DWARF5 version of attribute form not handled yet (loclistx)"),
            AttributeForm::DW_FORM_rnglistx => todo!("DWARF5 version of attribute form not handled yet (rnglistx)"),
            AttributeForm::DW_FORM_ref_sup8 => todo!("DWARF5 version of attribute form not handled yet (ref_sup8)"),
            AttributeForm::DW_FORM_strx1 => todo!("DWARF5 version of attribute form not handled yet (strx1)"),
            AttributeForm::DW_FORM_strx2 => todo!("DWARF5 version of attribute form not handled yet (strx2)"),
            AttributeForm::DW_FORM_strx3 => todo!("DWARF5 version of attribute form not handled yet (strx3)"),
            AttributeForm::DW_FORM_strx4 => todo!("DWARF5 version of attribute form not handled yet (strx4)"),
            AttributeForm::DW_FORM_addrx1 => todo!("DWARF5 version of attribute form not handled yet (addrx1)"),
            AttributeForm::DW_FORM_addrx2 => todo!("DWARF5 version of attribute form not handled yet (addrx2)"),
            AttributeForm::DW_FORM_addrx3 => todo!("DWARF5 version of attribute form not handled yet (addrx3)"),
            AttributeForm::DW_FORM_addrx4 => todo!("DWARF5 version of attribute form not handled yet (addrx4)"),
        }
        // means we did not encounter indirection
    }
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
#[repr(u16)]
pub enum Attribute {
    DW_AT_sibling = 0x01,
    DW_AT_location = 0x02,
    DW_AT_name = 0x03,
    Reserved1 = 0x04,
    Reserved2 = 0x05,
    Reserved3 = 0x06,
    Reserved4 = 0x07,
    Reserved5 = 0x08,
    DW_AT_ordering = 0x09,
    Reserved6 = 0x0a,
    DW_AT_byte_size = 0x0b,
    Reserved7 = 0x0c2,
    DW_AT_bit_size = 0x0d,
    Reserved8 = 0x0e,
    Reserved9 = 0x0f,
    DW_AT_stmt_list = 0x10,
    DW_AT_low_pc = 0x11,
    DW_AT_high_pc = 0x12,
    DW_AT_language = 0x13,
    Reserved10 = 0x14,
    DW_AT_discr = 0x15,
    DW_AT_discr_value = 0x16,
    DW_AT_visibility = 0x17,
    DW_AT_import = 0x18,
    DW_AT_string_length = 0x19,
    DW_AT_common_reference = 0x1a,
    DW_AT_comp_dir = 0x1b,
    DW_AT_const_value = 0x1c,
    DW_AT_containing_type = 0x1d,
    DW_AT_default_value = 0x1e,
    Reserved11 = 0x1f,
    DW_AT_inline = 0x20,
    DW_AT_is_optional = 0x21,
    DW_AT_lower_bound = 0x22,
    Reserved12 = 0x23,
    Reserved13 = 0x24,
    DW_AT_producer = 0x25,
    Reserved14 = 0x26,
    DW_AT_prototyped = 0x27,
    Reserved15 = 0x28,
    Reserved16 = 0x29,
    DW_AT_return_addr = 0x2a,
    Reserved17 = 0x2b,
    DW_AT_start_scope = 0x2c,
    Reserved18 = 0x2d,
    DW_AT_bit_stride = 0x2e,
    DW_AT_upper_bound = 0x2f,
    Reserved19 = 0x30,
    DW_AT_abstract_origin = 0x31,
    DW_AT_accessibility = 0x32,
    DW_AT_address_class = 0x33,
    DW_AT_artificial = 0x34,
    DW_AT_base_types = 0x35,
    DW_AT_calling_convention = 0x36,
    DW_AT_count = 0x37,
    DW_AT_data_member_location = 0x38,
    DW_AT_decl_column = 0x39,
    DW_AT_decl_file = 0x3a,
    DW_AT_decl_line = 0x3b,
    DW_AT_declaration = 0x3c,
    DW_AT_discr_list = 0x3d,
    DW_AT_encoding = 0x3e,
    DW_AT_external = 0x3f,
    DW_AT_frame_base = 0x40,
    DW_AT_friend = 0x41,
    DW_AT_identifier_case = 0x42,
    Reserved20 = 0x433,
    DW_AT_namelist_item = 0x44,
    DW_AT_priority = 0x45,
    DW_AT_segment = 0x46,
    DW_AT_specification = 0x47,
    DW_AT_static_link = 0x48,
    DW_AT_type = 0x49,
    DW_AT_use_location = 0x4a,
    DW_AT_variable_parameter = 0x4b,
    DW_AT_virtuality = 0x4c,
    DW_AT_vtable_elem_location = 0x4d,
    DW_AT_allocated = 0x4e,
    DW_AT_associated = 0x4f,
    DW_AT_data_location = 0x50,
    DW_AT_byte_stride = 0x51,
    DW_AT_entry_pc = 0x52,
    DW_AT_use_UTF8 = 0x53,
    DW_AT_extension = 0x54,
    DW_AT_ranges = 0x55,
    DW_AT_trampoline = 0x56,
    DW_AT_call_column = 0x57,
    DW_AT_call_file = 0x58,
    DW_AT_call_line = 0x59,
    DW_AT_description = 0x5a,
    DW_AT_binary_scale = 0x5b,
    DW_AT_decimal_scale = 0x5c,
    DW_AT_small = 0x5d,
    DW_AT_decimal_sign = 0x5e,
    DW_AT_digit_count = 0x5f,
    DW_AT_picture_string = 0x60,
    DW_AT_mutable = 0x61,
    DW_AT_threads_scaled = 0x62,
    DW_AT_explicit = 0x63,
    DW_AT_object_pointer = 0x64,
    DW_AT_endianity = 0x65,
    DW_AT_elemental = 0x66,
    DW_AT_pure = 0x67,
    DW_AT_recursive = 0x68,
    DW_AT_signature = 0x69,
    DW_AT_main_subprogram = 0x6a,
    DW_AT_data_bit_offset = 0x6b,
    DW_AT_const_expr = 0x6c,
    DW_AT_enum_class = 0x6d,
    DW_AT_linkage_name = 0x6e,
    DW_AT_string_length_bit_size = 0x6f,
    DW_AT_string_length_byte_size = 0x70,
    DW_AT_rank = 0x71,
    DW_AT_str_offsets_base = 0x72,
    DW_AT_addr_base = 0x73,
    DW_AT_rnglists_base = 0x74,
    Reserved21 = 0x75,
    DW_AT_dwo_name = 0x76,
    DW_AT_reference = 0x77,
    DW_AT_rvalue_reference = 0x78,
    DW_AT_macros = 0x79,
    DW_AT_call_all_calls = 0x7a,
    DW_AT_call_all_source_calls = 0x7b,
    DW_AT_call_all_tail_calls = 0x7c,
    DW_AT_call_return_pc = 0x7d,
    DW_AT_call_value = 0x7e,
    DW_AT_call_origin = 0x7f,
    DW_AT_call_parameter = 0x80,
    DW_AT_call_pc = 0x81,
    DW_AT_call_tail_call = 0x82,
    DW_AT_call_target = 0x83,
    DW_AT_call_target_clobbered = 0x84,
    DW_AT_call_data_location = 0x85,
    DW_AT_call_data_value = 0x86,
    DW_AT_noreturn = 0x87,
    DW_AT_alignment = 0x88,
    DW_AT_export_symbols = 0x89,
    DW_AT_deleted = 0x8a,
    DW_AT_defaulted = 0x8b,
    DW_AT_loclists_base = 0x8c,
    DW_AT_lo_user = 0x2000,
    DW_AT_hi_user = 0x3fff,
}
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
#[repr(u8)]
pub enum AttributeForm {
    DW_FORM_addr = 0x01,
    Reserved = 0x02,
    DW_FORM_block2 = 0x03,
    DW_FORM_block4 = 0x04,
    DW_FORM_data2 = 0x05,
    DW_FORM_data4 = 0x06,
    DW_FORM_data8 = 0x07,
    DW_FORM_string = 0x08,
    DW_FORM_block = 0x09,
    DW_FORM_block1 = 0x0a,
    DW_FORM_data1 = 0x0b,
    DW_FORM_flag = 0x0c,
    DW_FORM_sdata = 0x0d,
    DW_FORM_strp = 0x0e,
    DW_FORM_udata = 0x0f,
    DW_FORM_ref_addr = 0x10,
    DW_FORM_ref1 = 0x11,
    DW_FORM_ref2 = 0x12,
    DW_FORM_ref4 = 0x13,
    DW_FORM_ref8 = 0x14,
    DW_FORM_ref_udata = 0x15,
    DW_FORM_indirect = 0x16,
    DW_FORM_sec_offset = 0x17,
    DW_FORM_exprloc = 0x18,
    DW_FORM_flag_present = 0x19,
    DW_FORM_strx = 0x1a,
    DW_FORM_addrx = 0x1b,
    DW_FORM_ref_sup4 = 0x1c,
    DW_FORM_strp_sup = 0x1d,
    DW_FORM_data16 = 0x1e,
    DW_FORM_line_strp = 0x1f,
    DW_FORM_ref_sig8 = 0x20,
    DW_FORM_implicit_const = 0x21,
    DW_FORM_loclistx = 0x22,
    DW_FORM_rnglistx = 0x23,
    DW_FORM_ref_sup8 = 0x24,
    DW_FORM_strx1 = 0x25,
    DW_FORM_strx2 = 0x26,
    DW_FORM_strx3 = 0x27,
    DW_FORM_strx4 = 0x28,
    DW_FORM_addrx1 = 0x29,
    DW_FORM_addrx2 = 0x2a,
    DW_FORM_addrx3 = 0x2b,
    DW_FORM_addrx4 = 0x2c,
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u16)]
pub enum BaseTypeAttributeEncoding {
    DW_ATE_address = 0x01,
    DW_ATE_boolean = 0x02,
    DW_ATE_complex_float = 0x03,
    DW_ATE_float = 0x04,
    DW_ATE_signed = 0x05,
    DW_ATE_signed_char = 0x06,
    DW_ATE_unsigned = 0x07,
    DW_ATE_unsigned_char = 0x08,
    DW_ATE_imaginary_float = 0x09,
    DW_ATE_packed_decimal = 0x0a,
    DW_ATE_numeric_string = 0x0b,
    DW_ATE_edited = 0x0c,
    DW_ATE_signed_fixed = 0x0d,
    DW_ATE_unsigned_fixed = 0x0e,
    DW_ATE_decimal_float = 0x0f,
    DW_ATE_UTF = 0x10,
    DW_ATE_UCS = 0x11,
    DW_ATE_ASCII = 0x12,
    DW_ATE_lo_user = 0x80,
    DW_ATE_hi_user = 0xff,
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u16)]
pub enum DecimalSignEncoding {
    DW_DS_unsigned = 0x01,
    DW_DS_leading_overpunch = 0x02,
    DW_DS_trailing_overpunch = 0x03,
    DW_DS_leading_separate = 0x04,
    DW_DS_trailing_separate = 0x05,
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u16)]
pub enum EndianityEncoding {
    DW_END_default = 0x00,
    DW_END_big = 0x01,
    DW_END_little = 0x02,
    DW_END_lo_user = 0x40,
    DW_END_hi_user = 0xff,
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u16)]
pub enum AccessibilityEncoding {
    DW_ACCESS_public = 0x01,
    DW_ACCESS_protected = 0x02,
    DW_ACCESS_private = 0x03,
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u16)]
pub enum VisibilityEncoding {
    DW_VIS_local = 0x01,
    DW_VIS_exported = 0x02,
    DW_VIS_qualified = 0x03,
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u16)]
pub enum VirtualityEncoding {
    DW_VIRTUALITY_none = 0x00,
    DW_VIRTUALITY_virtual = 0x01,
    DW_VIRTUALITY_pure_virtual = 0x02,
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u16)]
pub enum SourceLanguage {
    DW_LANG_C89 = 0x0001,
    DW_LANG_C = 0x0002,
    DW_LANG_Ada83 = 0x0003,
    DW_LANG_C_plus_plus = 0x0004,
    DW_LANG_Cobol74 = 0x0005,
    DW_LANG_Cobol85 = 0x0006,
    DW_LANG_Fortran77 = 0x0007,
    DW_LANG_Fortran90 = 0x0008,
    DW_LANG_Pascal83 = 0x0009,
    DW_LANG_Modula2 = 0x000a,
    DW_LANG_Java = 0x000b,
    DW_LANG_C99 = 0x000c,
    DW_LANG_Ada95 = 0x000d,
    DW_LANG_Fortran95 = 0x000e,
    DW_LANG_PLI = 0x000f,
    DW_LANG_ObjC = 0x0010,
    DW_LANG_ObjC_plus_plus = 0x0011,
    DW_LANG_UPC = 0x0012,
    DW_LANG_D = 0x0013,
    DW_LANG_Python = 0x0014,
    DW_LANG_OpenCL = 0x0015,
    DW_LANG_Go = 0x0016,
    DW_LANG_Modula3 = 0x0017,
    DW_LANG_Haskell = 0x0018,
    DW_LANG_C_plus_plus_03 = 0x0019,
    DW_LANG_C_plus_plus_11 = 0x001a,
    DW_LANG_OCaml = 0x001b,
    DW_LANG_Rust = 0x001c,
    DW_LANG_C11 = 0x001d,
    DW_LANG_Swift = 0x001e,
    DW_LANG_Julia = 0x001f,
    DW_LANG_Dylan = 0x0020,
    DW_LANG_C_plus_plus_14 = 0x0021,
    DW_LANG_Fortran03 = 0x0022,
    DW_LANG_Fortran08 = 0x0023,
    DW_LANG_RenderScript = 0x0024,
    DW_LANG_BLISS = 0x0025,
    DW_LANG_lo_user = 0x8000,
    DW_LANG_hi_user = 0xffff,
}

pub enum IdentifierCaseEncoding {
    DW_ID_case_sensitive = 0x00,
    DW_ID_up_case = 0x01,
    DW_ID_down_case = 0x02,
    DW_ID_case_insensitive = 0x03,
}

pub enum CallingConventionEncoding {
    DW_CC_normal = 0x01,
    DW_CC_program = 0x02,
    DW_CC_nocall = 0x03,
    DW_CC_pass_by_reference = 0x04,
    DW_CC_pass_by_value = 0x05,
    DW_CC_lo_user = 0x40,
    DW_CC_hi_user = 0xff,
}

pub enum InlineCodes {
    DW_INL_not_inlined = 0x00,
    DW_INL_inlined = 0x01,
    DW_INL_declared_not_inlined = 0x02,
    DW_INL_declared_inlined = 0x03,
}

pub enum ArrayOrdering {
    DW_ORD_row_major = 0x00,
    DW_ORD_col_major = 0x01,
}

pub enum DiscriminantList {
    DW_DSC_label = 0x00,
    DW_DSC_range = 0x01,
}

pub enum NameIndexTable {
    DW_IDX_compile_unit = 1,
    DW_IDX_type_unit = 2,
    DW_IDX_die_offset = 3,
    DW_IDX_parent = 4,
    DW_IDX_type_hash = 5,
    DW_IDX_lo_user = 0x2000,
    DW_IDX_hi_user = 0x3fff,
}

pub enum DefaultedMemberEncoding {
    DW_DEFAULTED_no = 0x00,
    DW_DEFAULTED_in_class = 0x01,
    DW_DEFAULTED_out_of_class = 0x02,
}
