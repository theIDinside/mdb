#![allow(unused, non_camel_case_types)]
pub enum LocationListEncoding {
    DW_LLE_end_of_list = 0x00,
    DW_LLE_base_addressx = 0x01,
    DW_LLE_startx_endx = 0x02,
    DW_LLE_startx_length = 0x03,
    DW_LLE_offset_pair = 0x04,
    DW_LLE_default_location = 0x05,
    DW_LLE_base_address = 0x06,
    DW_LLE_start_end = 0x07,
    DW_LLE_start_length = 0x08,
}

pub struct LocationListHeader {
    initial_length: super::InitialLengthField,
    version: u16,
    address_size: u8,
    segment_selector_size: u8,
    /// describes the size of an array of offsets, immediately following the header in the byte stream,
    /// after this array, follows a series of range lists
    offset_entry_count: u32,
}

impl LocationListHeader {
    pub fn from_bytes(bytes: &[u8]) -> Self {
        let initial_length = super::InitialLengthField::from_bytes(bytes);
        let mut byte_offset = initial_length.offsets_bytes();
        let version = unsafe {
            let mut buf = [0u8; 2];
            std::ptr::copy_nonoverlapping(bytes.as_ptr().offset(byte_offset as _), buf.as_mut_ptr(), 2);
            std::mem::transmute::<[u8; 2], u16>(buf)
        };
        byte_offset += 2;
        let (address_size, segment_selector_size) = (bytes[byte_offset], bytes[byte_offset + 1]);
        byte_offset += 2;
        let offset_entry_count = unsafe {
            let mut buf = [0u8; 4];
            std::ptr::copy_nonoverlapping(bytes.as_ptr().offset(byte_offset as _), buf.as_mut_ptr(), 4);
            std::mem::transmute::<[u8; 4], u32>(buf)
        };

        Self {
            initial_length,
            version,
            address_size,
            segment_selector_size,
            offset_entry_count,
        }
    }
}
