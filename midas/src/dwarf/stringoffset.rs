pub struct Header {
    initial_length: super::InitialLengthField,
    version: u16,
    padding: u16,
}

// All these impl's are bound to change. They just implement the easy, naivest version possible. We don't care about unnecessary
// copies, we don't care about any of that bullshit. We just care about understanding the data for now.
impl Header {
    pub fn from_bytes(bytes: &[u8]) -> Header {
        let initial_length = super::InitialLengthField::from_bytes(bytes);
        let byte_offs = initial_length.length();
        let version = unsafe {
            let mut buf = [0u8; 2];
            std::ptr::copy_nonoverlapping(bytes.as_ptr().offset(byte_offs as _), buf.as_mut_ptr(), 2);
            std::mem::transmute::<[u8; 2], u16>(buf)
        };
        let padding = 0;
        Header {
            initial_length,
            version,
            padding,
        }
    }

    /// Get string offset value in the string offset table this header is responsible for
    /// * `bytes` - the raw bytes from which we should interpret from. When passing the slice, one should make sure that
    /// the data we're reading is actually correct, so that bytes[0] is where the Header information is found from.
    /// * `index` - the string table offset index for which string table offset we want to read
    /// * returns the offset found at position `index` in the string offset table
    pub fn get_offset(&self, bytes: &[u8], index: usize) -> usize {
        match self.initial_length {
            super::InitialLengthField::Dwarf32(_) => {
                let begin = 4;
                let byte_index = begin + index * 4;
                let res = unsafe {
                    let mut buf = [0u8; 4];
                    std::ptr::copy(bytes.as_ptr().offset(byte_index as _), buf.as_mut_ptr(), 4);
                    std::mem::transmute::<[u8; 4], u32>(buf)
                };
                res as usize
            }
            super::InitialLengthField::Dwarf64(_) => {
                let begin = 12;
                let byte_index = begin + index * 8;
                let res = unsafe {
                    let mut buf = [0u8; 8];
                    std::ptr::copy(bytes.as_ptr().offset(byte_index as _), buf.as_mut_ptr(), 8);
                    std::mem::transmute::<[u8; 8], u64>(buf)
                };
                res as usize
            }
        }
    }
}
