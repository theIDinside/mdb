use std::fmt;

#[derive(PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Address(pub usize);

impl Address {
    #[inline]
    pub fn value(&self) -> usize {
        self.0
    }
}

impl fmt::LowerHex for Address {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let val = self.0;
        fmt::LowerHex::fmt(&val, f) // delegate to i32's implementation
    }
}

impl fmt::UpperHex for Address {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let val = self.0;
        fmt::UpperHex::fmt(&val, f) // delegate to i32's implementation
    }
}
