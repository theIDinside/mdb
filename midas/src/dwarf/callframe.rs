pub struct CIEHeader {}

pub struct CIE {
    header: CIEHeader,
}

pub enum CallFrameOp {
    DW_CFA_advance_loc = 0b0100_0000,
    DW_CFA_offset = 0b1000_0000,
    DW_CFA_restore = 0x3,
    DW_CFA_nop = 0,
    DW_CFA_set_loc = 0,
    DW_CFA_advance_loc1 = 0,
    DW_CFA_advance_loc2 = 0,
    DW_CFA_advance_loc4 = 0,
    DW_CFA_offset_extended = 0,
    DW_CFA_restore_extended = 0,
    DW_CFA_undefined = 0,
    DW_CFA_same_value = 0,
    DW_CFA_register = 0,
    DW_CFA_remember_state = 0,
    DW_CFA_restore_state = 0,
    DW_CFA_def_cfa = 0,
    DW_CFA_def_cfa_register = 0,
    DW_CFA_def_cfa_offset = 0,
    DW_CFA_def_cfa_expression = 0,
    DW_CFA_expression = 0,
    DW_CFA_offset_extended_sf = 0,
    DW_CFA_def_cfa_sf = 0,
    DW_CFA_def_cfa_offset_sf = 0,
    DW_CFA_val_offset = 0,
    DW_CFA_val_offset_sf = 0,
    DW_CFA_val_expression = 0,
    DW_CFA_lo_user = 0,
    DW_CFA_hi_user = 0,
}
