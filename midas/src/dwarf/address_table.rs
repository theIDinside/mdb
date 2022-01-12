#[allow(unused)]
pub struct AddressTableHeader {
    initial_length: super::InitialLengthField,
    version: u16,
    address_size: u8,
    segment_selector_size: u8,
}

impl AddressTableHeader {
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
        Self {
            initial_length,
            version,
            address_size,
            segment_selector_size,
        }
    }
}
