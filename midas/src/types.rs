use std::fmt;

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
