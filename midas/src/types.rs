use std::{
    fmt,
    ops::{Deref, DerefMut},
};

// Macro that wraps types, for clarity in the API what they are meant to represent.
macro_rules! BasicTypeTuple {
    ($struct_name:ident($wrapped_type:ty)) => {
        #[derive(Debug, PartialEq, PartialOrd, Ord, Eq, Clone, Copy)]
        pub struct $struct_name(pub $wrapped_type);

        impl AsRef<$wrapped_type> for $struct_name {
            fn as_ref(&self) -> &usize {
                &self.0
            }
        }

        impl AsMut<$wrapped_type> for $struct_name {
            fn as_mut(&mut self) -> &mut $wrapped_type {
                &mut self.0
            }
        }

        impl Deref for $struct_name {
            type Target = $wrapped_type;
            fn deref(&self) -> &Self::Target {
                &self.0
            }
        }

        impl DerefMut for $struct_name {
            fn deref_mut(&mut self) -> &mut Self::Target {
                &mut self.0
            }
        }

        impl Into<$struct_name> for $wrapped_type {
            fn into(self) -> $struct_name {
                $struct_name(self)
            }
        }
    };
}

#[derive(PartialEq, Eq, PartialOrd, Ord, Hash, Clone, Copy)]
pub struct Address(pub usize);

impl Into<Address> for usize {
    fn into(self) -> Address {
        Address(self)
    }
}

impl Address {
    #[inline]
    pub fn value(&self) -> usize {
        self.0
    }
}

impl std::fmt::Debug for Address {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let val = self.0;
        write!(f, "Address(0x{:X})", val)
    }
}

impl fmt::LowerHex for Address {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let val = self.0;
        fmt::UpperHex::fmt(&val, f) // delegate to i32's implementation
    }
}

impl fmt::UpperHex for Address {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let val = self.0;
        fmt::UpperHex::fmt(&val, f) // delegate to i32's implementation
    }
}

BasicTypeTuple!(Index(usize));

#[derive(Debug)]
pub struct SectionPointer {
    ptr: *const u8,
    size: usize,
}

impl SectionPointer {
    pub unsafe fn from_raw(ptr: *const u8, size: usize) -> SectionPointer {
        SectionPointer { ptr, size }
    }

    pub fn from_slice(slice: &[u8]) -> SectionPointer {
        let ptr = slice.as_ptr();
        let size = slice.len();
        SectionPointer { ptr, size }
    }

    pub fn data(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.ptr, self.size) }
    }

    pub fn len(&self) -> usize {
        self.size
    }
}
