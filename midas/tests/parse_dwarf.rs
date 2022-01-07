#![allow(unused, unused_macros)]
use std::{panic, process::Command, sync::Once};

use midas::{dwarf::attributes, leb128::decode_unsigned};

static BUILT_TEST_DEBUGGEES: Once = Once::new();

macro_rules! tests_dir {
    () => {
        concat!(env!("CARGO_MANIFEST_DIR"), "/tests/subjects")
    };
}

macro_rules! subjects {
    () => {
        concat!(tests_dir!(), "/executables")
    };
    ($e: expr) => {
        concat!(concat!(tests_dir!(), "/executables/"), $e)
    };
}

pub fn compile_subjects() {
    BUILT_TEST_DEBUGGEES.call_once(|| {
        let status = Command::new("make")
            .arg("all")
            .current_dir(tests_dir!())
            .spawn()
            .unwrap()
            .wait()
            .unwrap();
        assert!(status.success())
    });
}

fn run_test<T>(test: T) -> ()
where
    T: FnOnce() -> () + panic::UnwindSafe,
{
    compile_subjects();
    let result = panic::catch_unwind(|| test());
    assert!(result.is_ok())
}
// binary data taken from myfile1.c
const DEBUG_INFO: &[u8] = &[
    // COMPILATION UNIT HEADER BEGIN
    0x21, 0x00, 0x00, 0x00, // unit_length, length of this entry, *excluding* the bytes of the initial_length field
    0x04, 0x00, // version
    0x00, 0x00, 0x00, 0x00, // debug_abbrev offset
    0x08, // pointer size
    // COMPILATION UNIT HEADER END

    // CONTRIBUTION 1 BEGIN
    0x01, 0x00, 0x00, 0x00, //
    0x00, 0x0C, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x02, 0x01, //
    0x06, 0x00, 0x00, 0x00, //
    0x00, 0x00,
    // CONTRIBUTION 1 END
];

const DEBUG_ABBREV: &[u8] = &[
    0x01, 0x11, // DW_TAG_COMPILE_UNIT
    0x01, 0x25, 0x0E, 0x13, 0x0B, 0x03, 0x0E, 0x1B, 0x0E, 0x10, 0x17, 0x00, 0x00, 0x02, 0x24, 0x00, 0x0B, 0x0B, 0x3E,
    0x0B, 0x03, 0x0E, 0x00, 0x00, 0x00,
];

const DEBUG_STR: &[u8] = &[
    0x47, 0x4E, 0x55, 0x20, 0x43, 0x31, 0x37, 0x20, 0x31, 0x30, 0x2E, 0x33, 0x2E, 0x30, 0x20, 0x2D, 0x6D, 0x74, 0x75,
    0x6E, 0x65, 0x3D, 0x67, 0x65, 0x6E, 0x65, 0x72, 0x69, 0x63, 0x20, 0x2D, 0x6D, 0x61, 0x72, 0x63, 0x68, 0x3D, 0x78,
    0x38, 0x36, 0x2D, 0x36, 0x34, 0x20, 0x2D, 0x67, 0x20, 0x2D, 0x66, 0x61, 0x73, 0x79, 0x6E, 0x63, 0x68, 0x72, 0x6F,
    0x6E, 0x6F, 0x75, 0x73, 0x2D, 0x75, 0x6E, 0x77, 0x69, 0x6E, 0x64, 0x2D, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x73, 0x20,
    0x2D, 0x66, 0x73, 0x74, 0x61, 0x63, 0x6B, 0x2D, 0x70, 0x72, 0x6F, 0x74, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x2D, 0x73,
    0x74, 0x72, 0x6F, 0x6E, 0x67, 0x20, 0x2D, 0x66, 0x73, 0x74, 0x61, 0x63, 0x6B, 0x2D, 0x63, 0x6C, 0x61, 0x73, 0x68,
    0x2D, 0x70, 0x72, 0x6F, 0x74, 0x65, 0x63, 0x74, 0x69, 0x6F, 0x6E, 0x20, 0x2D, 0x66, 0x63, 0x66, 0x2D, 0x70, 0x72,
    0x6F, 0x74, 0x65, 0x63, 0x74, 0x69, 0x6F, 0x6E, 0x00, 0x64, 0x77, 0x61, 0x72, 0x66, 0x5F, 0x73, 0x74, 0x61, 0x6E,
    0x64, 0x61, 0x72, 0x64, 0x5F, 0x65, 0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x73, 0x2F, 0x64, 0x31, 0x2F, 0x6D, 0x79,
    0x66, 0x69, 0x6C, 0x65, 0x31, 0x2E, 0x63, 0x00, 0x2F, 0x68, 0x6F, 0x6D, 0x65, 0x2F, 0x63, 0x78, 0x2F, 0x64, 0x65,
    0x76, 0x2F, 0x6F, 0x70, 0x65, 0x6E, 0x73, 0x6F, 0x75, 0x72, 0x63, 0x65, 0x2F, 0x6D, 0x64, 0x65, 0x62, 0x75, 0x67,
    0x2F, 0x6D, 0x69, 0x64, 0x61, 0x73, 0x2F, 0x74, 0x65, 0x73, 0x74, 0x73, 0x2F, 0x73, 0x75, 0x62, 0x6A, 0x65, 0x63,
    0x74, 0x73, 0x00, 0x63, 0x68, 0x61, 0x72, 0x00,
];

// 0x11, 0x25
#[test]
pub fn parse_debug_info() {
    /*
    assert_eq!(DEBUG_STR.len(), 236);
    let cu_header = midas::dwarf::compilation_unit::CompilationUnitHeader::from_bytes(DEBUG_INFO);
    println!("Compilation Unit Header #1:\n {:?}", cu_header);
    let contribution_begins = cu_header.stride();
    assert_eq!(contribution_begins, 11);

    let abbr_entries = attributes::parse_attributes(&DEBUG_ABBREV).unwrap();
    println!("{:#?}", abbr_entries);
    let encoding = decode_unsigned(&DEBUG_INFO[cu_header.stride()..]).unwrap();
    assert_eq!(encoding.value, 1);
    let entry = abbr_entries.get(&encoding.value).unwrap();
    let mut offset = cu_header.stride() + encoding.bytes_read;
    let mut string_entry_index = 0;
    for (attribute, form) in entry.attrs_list.iter() {
        match form {
            attributes::AttributeForm::DW_FORM_addr => todo!(),
            attributes::AttributeForm::Reserved => todo!(),
            attributes::AttributeForm::DW_FORM_block2 => todo!(),
            attributes::AttributeForm::DW_FORM_block4 => todo!(),
            attributes::AttributeForm::DW_FORM_data2 => todo!(),
            attributes::AttributeForm::DW_FORM_data4 => todo!(),
            attributes::AttributeForm::DW_FORM_data8 => todo!(),
            attributes::AttributeForm::DW_FORM_string => todo!(),
            attributes::AttributeForm::DW_FORM_block => todo!(),
            attributes::AttributeForm::DW_FORM_block1 => todo!(),
            attributes::AttributeForm::DW_FORM_data1 => match attribute {
                attributes::Attribute::DW_AT_sibling => todo!(),
                attributes::Attribute::DW_AT_location => todo!(),
                attributes::Attribute::DW_AT_name => todo!(),
                attributes::Attribute::Reserved1 => todo!(),
                attributes::Attribute::Reserved2 => todo!(),
                attributes::Attribute::Reserved3 => todo!(),
                attributes::Attribute::Reserved4 => todo!(),
                attributes::Attribute::Reserved5 => todo!(),
                attributes::Attribute::DW_AT_ordering => todo!(),
                attributes::Attribute::Reserved6 => todo!(),
                attributes::Attribute::DW_AT_byte_size => todo!(),
                attributes::Attribute::Reserved7 => todo!(),
                attributes::Attribute::DW_AT_bit_size => todo!(),
                attributes::Attribute::Reserved8 => todo!(),
                attributes::Attribute::Reserved9 => todo!(),
                attributes::Attribute::DW_AT_stmt_list => todo!(),
                attributes::Attribute::DW_AT_low_pc => todo!(),
                attributes::Attribute::DW_AT_high_pc => todo!(),
                attributes::Attribute::DW_AT_language => {
                    let lang: attributes::SourceLanguage =
                        unsafe { std::mem::transmute([*&DEBUG_INFO[offset], *&DEBUG_INFO[offset + 1]]) };
                    println!("DW_AT_language : {:?}", lang);
                    offset += 2;
                }
                attributes::Attribute::Reserved10 => todo!(),
                attributes::Attribute::DW_AT_discr => todo!(),
                attributes::Attribute::DW_AT_discr_value => todo!(),
                attributes::Attribute::DW_AT_visibility => todo!(),
                attributes::Attribute::DW_AT_import => todo!(),
                attributes::Attribute::DW_AT_string_length => todo!(),
                attributes::Attribute::DW_AT_common_reference => todo!(),
                attributes::Attribute::DW_AT_comp_dir => todo!(),
                attributes::Attribute::DW_AT_const_value => todo!(),
                attributes::Attribute::DW_AT_containing_type => todo!(),
                attributes::Attribute::DW_AT_default_value => todo!(),
                attributes::Attribute::Reserved11 => todo!(),
                attributes::Attribute::DW_AT_inline => todo!(),
                attributes::Attribute::DW_AT_is_optional => todo!(),
                attributes::Attribute::DW_AT_lower_bound => todo!(),
                attributes::Attribute::Reserved12 => todo!(),
                attributes::Attribute::Reserved13 => todo!(),
                attributes::Attribute::DW_AT_producer => todo!(),
                attributes::Attribute::Reserved14 => todo!(),
                attributes::Attribute::DW_AT_prototyped => todo!(),
                attributes::Attribute::Reserved15 => todo!(),
                attributes::Attribute::Reserved16 => todo!(),
                attributes::Attribute::DW_AT_return_addr => todo!(),
                attributes::Attribute::Reserved17 => todo!(),
                attributes::Attribute::DW_AT_start_scope => todo!(),
                attributes::Attribute::Reserved18 => todo!(),
                attributes::Attribute::DW_AT_bit_stride => todo!(),
                attributes::Attribute::DW_AT_upper_bound => todo!(),
                attributes::Attribute::Reserved19 => todo!(),
                attributes::Attribute::DW_AT_abstract_origin => todo!(),
                attributes::Attribute::DW_AT_accessibility => todo!(),
                attributes::Attribute::DW_AT_address_class => todo!(),
                attributes::Attribute::DW_AT_artificial => todo!(),
                attributes::Attribute::DW_AT_base_types => todo!(),
                attributes::Attribute::DW_AT_calling_convention => todo!(),
                attributes::Attribute::DW_AT_count => todo!(),
                attributes::Attribute::DW_AT_data_member_location => todo!(),
                attributes::Attribute::DW_AT_decl_column => todo!(),
                attributes::Attribute::DW_AT_decl_file => todo!(),
                attributes::Attribute::DW_AT_decl_line => todo!(),
                attributes::Attribute::DW_AT_declaration => todo!(),
                attributes::Attribute::DW_AT_discr_list => todo!(),
                attributes::Attribute::DW_AT_encoding => todo!(),
                attributes::Attribute::DW_AT_external => todo!(),
                attributes::Attribute::DW_AT_frame_base => todo!(),
                attributes::Attribute::DW_AT_friend => todo!(),
                attributes::Attribute::DW_AT_identifier_case => todo!(),
                attributes::Attribute::Reserved20 => todo!(),
                attributes::Attribute::DW_AT_namelist_item => todo!(),
                attributes::Attribute::DW_AT_priority => todo!(),
                attributes::Attribute::DW_AT_segment => todo!(),
                attributes::Attribute::DW_AT_specification => todo!(),
                attributes::Attribute::DW_AT_static_link => todo!(),
                attributes::Attribute::DW_AT_type => todo!(),
                attributes::Attribute::DW_AT_use_location => todo!(),
                attributes::Attribute::DW_AT_variable_parameter => todo!(),
                attributes::Attribute::DW_AT_virtuality => todo!(),
                attributes::Attribute::DW_AT_vtable_elem_location => todo!(),
                attributes::Attribute::DW_AT_allocated => todo!(),
                attributes::Attribute::DW_AT_associated => todo!(),
                attributes::Attribute::DW_AT_data_location => todo!(),
                attributes::Attribute::DW_AT_byte_stride => todo!(),
                attributes::Attribute::DW_AT_entry_pc => todo!(),
                attributes::Attribute::DW_AT_use_UTF8 => todo!(),
                attributes::Attribute::DW_AT_extension => todo!(),
                attributes::Attribute::DW_AT_ranges => todo!(),
                attributes::Attribute::DW_AT_trampoline => todo!(),
                attributes::Attribute::DW_AT_call_column => todo!(),
                attributes::Attribute::DW_AT_call_file => todo!(),
                attributes::Attribute::DW_AT_call_line => todo!(),
                attributes::Attribute::DW_AT_description => todo!(),
                attributes::Attribute::DW_AT_binary_scale => todo!(),
                attributes::Attribute::DW_AT_decimal_scale => todo!(),
                attributes::Attribute::DW_AT_small => todo!(),
                attributes::Attribute::DW_AT_decimal_sign => todo!(),
                attributes::Attribute::DW_AT_digit_count => todo!(),
                attributes::Attribute::DW_AT_picture_string => todo!(),
                attributes::Attribute::DW_AT_mutable => todo!(),
                attributes::Attribute::DW_AT_threads_scaled => todo!(),
                attributes::Attribute::DW_AT_explicit => todo!(),
                attributes::Attribute::DW_AT_object_pointer => todo!(),
                attributes::Attribute::DW_AT_endianity => todo!(),
                attributes::Attribute::DW_AT_elemental => todo!(),
                attributes::Attribute::DW_AT_pure => todo!(),
                attributes::Attribute::DW_AT_recursive => todo!(),
                attributes::Attribute::DW_AT_signature => todo!(),
                attributes::Attribute::DW_AT_main_subprogram => todo!(),
                attributes::Attribute::DW_AT_data_bit_offset => todo!(),
                attributes::Attribute::DW_AT_const_expr => todo!(),
                attributes::Attribute::DW_AT_enum_class => todo!(),
                attributes::Attribute::DW_AT_linkage_name => todo!(),
                attributes::Attribute::DW_AT_string_length_bit_size => todo!(),
                attributes::Attribute::DW_AT_string_length_byte_size => todo!(),
                attributes::Attribute::DW_AT_rank => todo!(),
                attributes::Attribute::DW_AT_str_offsets_base => todo!(),
                attributes::Attribute::DW_AT_addr_base => todo!(),
                attributes::Attribute::DW_AT_rnglists_base => todo!(),
                attributes::Attribute::Reserved21 => todo!(),
                attributes::Attribute::DW_AT_dwo_name => todo!(),
                attributes::Attribute::DW_AT_reference => todo!(),
                attributes::Attribute::DW_AT_rvalue_reference => todo!(),
                attributes::Attribute::DW_AT_macros => todo!(),
                attributes::Attribute::DW_AT_call_all_calls => todo!(),
                attributes::Attribute::DW_AT_call_all_source_calls => todo!(),
                attributes::Attribute::DW_AT_call_all_tail_calls => todo!(),
                attributes::Attribute::DW_AT_call_return_pc => todo!(),
                attributes::Attribute::DW_AT_call_value => todo!(),
                attributes::Attribute::DW_AT_call_origin => todo!(),
                attributes::Attribute::DW_AT_call_parameter => todo!(),
                attributes::Attribute::DW_AT_call_pc => todo!(),
                attributes::Attribute::DW_AT_call_tail_call => todo!(),
                attributes::Attribute::DW_AT_call_target => todo!(),
                attributes::Attribute::DW_AT_call_target_clobbered => todo!(),
                attributes::Attribute::DW_AT_call_data_location => todo!(),
                attributes::Attribute::DW_AT_call_data_value => todo!(),
                attributes::Attribute::DW_AT_noreturn => todo!(),
                attributes::Attribute::DW_AT_alignment => todo!(),
                attributes::Attribute::DW_AT_export_symbols => todo!(),
                attributes::Attribute::DW_AT_deleted => todo!(),
                attributes::Attribute::DW_AT_defaulted => todo!(),
                attributes::Attribute::DW_AT_loclists_base => todo!(),
                attributes::Attribute::DW_AT_lo_user => todo!(),
                attributes::Attribute::DW_AT_hi_user => todo!(),
            },
            attributes::AttributeForm::DW_FORM_flag => todo!(),
            attributes::AttributeForm::DW_FORM_sdata => todo!(),
            attributes::AttributeForm::DW_FORM_strp => {
                let dbg_str_offset: u32 = unsafe {
                    let mut buf: [u8; 4] = [0; 4];
                    std::ptr::copy_nonoverlapping(DEBUG_INFO.as_ptr().offset(offset as _), buf.as_mut_ptr(), 4);
                    std::mem::transmute(buf)
                };

                let (begin, end) = if dbg_str_offset == 0 {
                    let r = *&DEBUG_STR[string_entry_index..]
                        .iter()
                        .position(|c| *c == 0)
                        .map(|value| dbg_str_offset as usize + value)
                        .unwrap();
                    let result = (string_entry_index, string_entry_index + r);
                    string_entry_index += r + 1;
                    result
                } else {
                    let e = *&DEBUG_STR[dbg_str_offset as usize..]
                        .iter()
                        .position(|c| *c == 0)
                        .map(|value| dbg_str_offset as usize + value)
                        .unwrap();
                    (dbg_str_offset as usize, e)
                };

                let s = std::str::from_utf8(&DEBUG_STR[begin..end]).unwrap();
                println!("{:?} : {}", attribute, s);
                offset += 4;
            }
            attributes::AttributeForm::DW_FORM_udata => todo!(),
            attributes::AttributeForm::DW_FORM_ref_addr => todo!(),
            attributes::AttributeForm::DW_FORM_ref1 => todo!(),
            attributes::AttributeForm::DW_FORM_ref2 => todo!(),
            attributes::AttributeForm::DW_FORM_ref4 => todo!(),
            attributes::AttributeForm::DW_FORM_ref8 => todo!(),
            attributes::AttributeForm::DW_FORM_ref_udata => todo!(),
            attributes::AttributeForm::DW_FORM_indirect => todo!(),
            attributes::AttributeForm::DW_FORM_sec_offset => todo!(),
            attributes::AttributeForm::DW_FORM_exprloc => todo!(),
            attributes::AttributeForm::DW_FORM_flag_present => todo!(),
            attributes::AttributeForm::DW_FORM_strx => todo!(),
            attributes::AttributeForm::DW_FORM_addrx => todo!(),
            attributes::AttributeForm::DW_FORM_ref_sup4 => todo!(),
            attributes::AttributeForm::DW_FORM_strp_sup => todo!(),
            attributes::AttributeForm::DW_FORM_data16 => todo!(),
            attributes::AttributeForm::DW_FORM_line_strp => todo!(),
            attributes::AttributeForm::DW_FORM_ref_sig8 => todo!(),
            attributes::AttributeForm::DW_FORM_implicit_const => todo!("implicit const not yet implemented"),
            attributes::AttributeForm::DW_FORM_loclistx => todo!(),
            attributes::AttributeForm::DW_FORM_rnglistx => todo!(),
            attributes::AttributeForm::DW_FORM_ref_sup8 => todo!(),
            attributes::AttributeForm::DW_FORM_strx1 => todo!(),
            attributes::AttributeForm::DW_FORM_strx2 => todo!(),
            attributes::AttributeForm::DW_FORM_strx3 => todo!(),
            attributes::AttributeForm::DW_FORM_strx4 => todo!(),
            attributes::AttributeForm::DW_FORM_addrx1 => todo!(),
            attributes::AttributeForm::DW_FORM_addrx2 => todo!(),
            attributes::AttributeForm::DW_FORM_addrx3 => todo!(),
            attributes::AttributeForm::DW_FORM_addrx4 => todo!(),
        }
    }
     */
}
