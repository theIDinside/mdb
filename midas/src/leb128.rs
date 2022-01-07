use crate::{MidasError, MidasSysResult};

// These algorithms; taken directly from the DWARF 5.0 standards examples for algorithms to encode and decode signed and unsigned LEB128's

const LEB128_MASK: u8 = 0b0111_1111;

pub struct DecodeResult<T> {
    pub value: T,
    pub bytes_read: usize,
}

// from https://dwarfstd.org/doc/DWARF5.pdf, page 284
pub fn decode_unsigned(bytes: &[u8]) -> MidasSysResult<DecodeResult<u64>> {
    let mut result = 0u64;
    let mut shift = 0u64;
    let mut index = 0usize;
    loop {
        let byte = bytes[index];
        result |= ((byte & LEB128_MASK) as u64) << shift;
        if shift == 63 && byte != 0x0 && byte != 0x1 {
            return Err(MidasError::BadUnsignedLEB128Encoding(index));
        }
        index += 1;
        if byte & !LEB128_MASK == 0 {
            return Ok(DecodeResult {
                value: result,
                bytes_read: index,
            });
        }
        shift += 7;
    }
}

// from https://dwarfstd.org/doc/DWARF5.pdf, page 285
pub fn decode_signed(bytes: &[u8]) -> MidasSysResult<DecodeResult<i64>> {
    let mut result = 0;
    let mut shift = 0;
    // 64 bits
    let size = 64;
    let mut idx = 0;
    let mut byte;
    'decode: loop {
        byte = bytes[idx];
        if shift == 63 && byte != 0x0 && byte != 0x7f {
            return Err(MidasError::BadSignedLEB128Encoding(idx));
        }
        result |= ((byte & LEB128_MASK) as i64) << shift;
        shift += 7;
        idx += 1;
        if byte & !LEB128_MASK == 0 {
            break 'decode;
        }
    }
    if shift < size && byte & !LEB128_MASK != 0 {
        result |= -(1 << shift);
    }
    Ok(DecodeResult {
        value: result,
        bytes_read: idx,
    })
}
