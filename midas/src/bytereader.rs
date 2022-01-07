use crate::MidasSysResult;

pub struct Reader<'a> {
    data: &'a [u8],
}

// I'm actually pretty happy with this interface.
impl<'a> Reader<'a> {
    pub fn wrap(data: &'a [u8]) -> Reader<'a> {
        Reader { data }
    }

    pub fn read_slice(&mut self, len: usize) -> MidasSysResult<&[u8]> {
        if self.data.len() >= len {
            let res = &self.data[..len];
            self.data = &self.data[len..];
            return Ok(self.data);
        }
        Err(super::MidasError::EOFNotExpected)
    }

    pub fn read_u8(&mut self) -> u8 {
        let res = self.data[0];
        self.flow(1);
        res
    }

    pub fn read_u16(&mut self) -> u16 {
        let res = unsafe {
            let mut buf = [0u8, 0u8];
            std::ptr::copy_nonoverlapping(self.data.as_ptr(), buf.as_mut_ptr(), 2);
            std::mem::transmute(buf)
        };
        self.flow(2);
        res
    }

    pub fn read_u32(&mut self) -> u32 {
        let res = unsafe {
            let mut buf = [0u8, 0u8, 0u8, 0u8];
            std::ptr::copy_nonoverlapping(self.data.as_ptr(), buf.as_mut_ptr(), 4);
            std::mem::transmute(buf)
        };
        self.flow(4);
        res
    }

    pub fn read_u64(&mut self) -> u64 {
        let res = unsafe {
            let mut buf = [0u8, 0u8, 0u8, 0u8, 0u8, 0u8, 0u8, 0u8];
            std::ptr::copy_nonoverlapping(self.data.as_ptr(), buf.as_mut_ptr(), 8);
            std::mem::transmute(buf)
        };
        self.flow(8);
        res
    }

    pub fn read_uleb128(&mut self) -> MidasSysResult<u64> {
        let leb = super::leb128::decode_unsigned(&self.data)?;
        self.flow(leb.bytes_read);
        Ok(leb.value)
    }

    pub fn read_ileb128(&mut self) -> MidasSysResult<i64> {
        let leb = super::leb128::decode_signed(&self.data)?;
        self.flow(leb.bytes_read);
        Ok(leb.value)
    }

    pub fn length(&self) -> usize {
        self.data.len()
    }

    fn flow(&mut self, offset: usize) {
        self.data = &self.data[offset..];
    }
}
