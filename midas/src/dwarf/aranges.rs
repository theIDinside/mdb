#[allow(unused)]
// Header for the entries in the table of address ranges; this (set of) data lives in .debug_aranges of the object file
pub struct AddressRangeHeader {
    //
    unit_length: super::InitialLengthField,
    version: u16,
    // regardless of if have a dwarf that's 32-bit or 64-bit format, storing it in usize doesn't matter. We have already
    // parsed it by that point.
    debug_info_offset: usize,
    // how many bytes does an address require to be represented on the target system
    address_size: u8,
    // how many bytes a segment selector is on the target system
    segment_selector_size: u8,
}

// following the header, is a series of tuples, each consist of (segment, address, length).
// the first tuple following the header.
// If segment_selector_size is 0; this field is eliminated from all tuples, thus they only consist of (address, length)
// The "null byte" of this list of tuples is (0, 0, 0) or (0, 0) if segment_selector_size = 0
