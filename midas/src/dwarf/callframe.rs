#![allow(unused, non_camel_case_types)]
pub struct CIEHeader {}

pub struct CIE {
    header: CIEHeader,
}

pub enum CallFrameOp {
    DW_CFA_advance_loc = 0b0100_0000,
    DW_CFA_offset = 0b1000_0000,
    DW_CFA_restore = 0b1100_0000,
    DW_CFA_nop = 0,
    DW_CFA_set_loc = 0x01,
    DW_CFA_advance_loc1 = 0x02,
    DW_CFA_advance_loc2 = 0x03,
    DW_CFA_advance_loc4 = 0x04,
    DW_CFA_offset_extended = 0x05,
    DW_CFA_restore_extended = 0x06,
    DW_CFA_undefined = 0x07,
    DW_CFA_same_value = 0x08,
    DW_CFA_register = 0x09,
    DW_CFA_remember_state = 0x0A,
    DW_CFA_restore_state = 0x0B,
    DW_CFA_def_cfa = 0x0C,
    DW_CFA_def_cfa_register = 0x0D,
    DW_CFA_def_cfa_offset = 0x0E,
    DW_CFA_def_cfa_expression = 0x0F,
    DW_CFA_expression = 0x10,
    DW_CFA_offset_extended_sf = 0x11,
    DW_CFA_def_cfa_sf = 0x12,
    DW_CFA_def_cfa_offset_sf = 0x13,
    DW_CFA_val_offset = 0x14,
    DW_CFA_val_offset_sf = 0x15,
    DW_CFA_val_expression = 0x16,
    DW_CFA_lo_user = 0x1c,
    DW_CFA_hi_user = 0x3f,
}
