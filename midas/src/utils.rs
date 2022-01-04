/// Takes a to_string'able type and to_string's it. Utility function, for shortening the code
/// that for instance, returns a Result<T, SomeErrorThatIsNotCompatibleWithMidasSysResult::Error> and converts it to our Error format (which currently is a String only, but in the future will change)
#[inline(always)]
pub fn midas_err<T: ToString>(to_stringable_error: T) -> String {
    to_stringable_error.to_string()
}
pub mod unchecked {
    pub unsafe fn as_mut_bytes<T: Sized>(p: &T) -> &mut [u8] {
        std::slice::from_raw_parts_mut((p as *const T) as *mut u8, ::std::mem::size_of::<T>())
    }

    pub unsafe fn as_bytes<T: Sized>(p: &T) -> &[u8] {
        std::slice::from_raw_parts((p as *const T) as *const u8, ::std::mem::size_of::<T>())
    }

    #[inline(always)]
    pub unsafe fn bytes_to_u16(bytes: &[u8]) -> u16 {
        let mut buf = [0u8; 2];
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.as_mut_ptr(), 2);
        std::mem::transmute(buf)
    }

    #[inline(always)]
    pub unsafe fn bytes_to_u32(bytes: &[u8]) -> u32 {
        let mut buf = [0u8; 4];
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.as_mut_ptr(), 4);
        std::mem::transmute(buf)
    }

    #[inline(always)]
    pub unsafe fn bytes_to_u64(bytes: &[u8]) -> u64 {
        let mut buf = [0u8; 8];
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.as_mut_ptr(), 8);
        std::mem::transmute(buf)
    }

    #[inline(always)]
    pub unsafe fn bytes_to_u128(bytes: &[u8]) -> u128 {
        let mut buf = [0u8; 16];
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.as_mut_ptr(), 16);
        std::mem::transmute(buf)
    }
}

pub mod checked {
    pub fn bytes_to_u16_checked(bytes: &[u8]) -> Option<u16> {
        if bytes.len() < 2 {
            return None;
        }
        Some(unsafe {
            let mut buf = [0u8; 2];
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.as_mut_ptr(), 2);
            std::mem::transmute(buf)
        })
    }

    pub fn bytes_to_u32_checked(bytes: &[u8]) -> Option<u32> {
        if bytes.len() < 4 {
            return None;
        }
        Some(unsafe {
            let mut buf = [0u8; 4];
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.as_mut_ptr(), 4);
            std::mem::transmute(buf)
        })
    }

    pub fn bytes_to_u64_checked(bytes: &[u8]) -> Option<u64> {
        if bytes.len() < 8 {
            return None;
        }
        let mut buf = [0u8; 8];
        Some(unsafe {
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.as_mut_ptr(), 8);
            std::mem::transmute(buf)
        })
    }

    pub fn bytes_to_u128_checked(bytes: &[u8]) -> Option<u128> {
        if bytes.len() < 16 {
            return None;
        }
        Some(unsafe {
            let mut buf = [0u8; 16];
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf.as_mut_ptr(), 16);
            std::mem::transmute(buf)
        })
    }
}
