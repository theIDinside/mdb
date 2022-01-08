use crate::MidasSysResult;

// Reader that "consumes" the data it points to, meaning, it's not a seekable reader.
pub struct ConsumeReader<'data> {
    data: &'data [u8],
}

// I'm actually pretty happy with this interface.
/// All read operations move the pointer to the data forwards and can not be moved backwards.
impl<'data> ConsumeReader<'data> {
    pub fn wrap(data: &'data [u8]) -> ConsumeReader<'data> {
        ConsumeReader { data }
    }

    pub fn read_slice(&mut self, len: usize) -> MidasSysResult<&[u8]> {
        if self.data.len() >= len {
            let res = &self.data[..len];
            self.data = &self.data[len..];
            return Ok(res);
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

    pub fn read_str(&mut self) -> MidasSysResult<&str> {
        let end = self.data.iter().position(|b| *b == 0);
        if let Some(pos) = end {
            let s = std::str::from_utf8(&self.data[..pos]);
            self.flow(pos);
            s.map_err(|e| crate::MidasError::from(e))
        } else {
            Err(crate::MidasError::UTF8Error {
                valid_up_to: self.data.len(),
                error_len: None,
            })
        }
    }

    fn flow(&mut self, offset: usize) {
        self.data = &self.data[offset..];
    }
}

pub struct NonConsumingReader<'data> {
    data: &'data [u8],
}

impl<'data> NonConsumingReader<'data> {
    pub fn new(storage: &'data [u8]) -> NonConsumingReader {
        NonConsumingReader { data: storage }
    }

    pub fn seek(&self, offset: usize) -> MidasSysResult<ConsumeReader> {
        if offset >= self.data.len() {
            Err(crate::MidasError::ReaderOutOfBounds)
        } else {
            Ok(ConsumeReader {
                data: &self.data[offset..],
            })
        }
    }

    pub fn read_str_from(&self, offset: usize) -> MidasSysResult<&'data str> {
        let end = self.data.iter().skip(offset).position(|b| *b == 0);
        if let Some(pos) = end {
            let s = std::str::from_utf8(&self.data[offset..offset + pos]);
            s.map_err(|e| crate::MidasError::from(e))
        } else {
            Err(crate::MidasError::UTF8Error {
                valid_up_to: self.data.len(),
                error_len: None,
            })
        }
    }
}
