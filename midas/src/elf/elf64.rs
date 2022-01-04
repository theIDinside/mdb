use nixwrap::MidasSysResult;

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Class {
    ELF32,
    ELF64,
    Invalid,
}
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum DataEncoding {
    // ELFDATANONE
    Unknown,
    // 2's complement, least significant byte; ELFDATA2LSB
    LSB,
    // 2's complement, most significant byte; ELFDATA2MSB
    MSB,
}
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Version {
    Invalid,
    Current,
}

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
#[allow(dead_code, non_camel_case_types)]
pub enum OperatingSystemABI {
    NONE_OR_SYSV, // UNIX System V ABI
    HPUX,         // HP-UX ABI
    NETBSD,       // NetBSD ABI
    GNU_LINUX,    // Linux ABI /GNU ELF extensions
    SOLARIS,      // Solaris ABI
    AIX,
    IRIX,    // IRIX ABI
    FREEBSD, // FreeBSD ABI
    TRU64,   // TRU64 UNIX ABI
    MODESTO,
    OPENBSD,
    ARM_E,
    ARM,        // ARM architecture ABI
    STANDALONE, // Stand-alone (embedded) ABI
    UNKNOWN,
}
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum ObjectType {
    Unknown,      // An unknown type.
    Relocatable,  // A relocatable file. (REL)
    Executable,   // An executable file. (EXEC)
    SharedObject, // A shared object. (DYN)
    Core,         // A core file. (CORE)
    LOProcessorSpecific,
    HIProcessorSpecific,
}

#[allow(dead_code, non_camel_case_types)]
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Machine {
    NONE,
    M32,
    SPARC,
    Intel386,
    Motorola68K,
    Motorola88K,
    IAMCU,
    Intel860,
    MIPS,
    S370,
    MIPS_RS3_LE,
    PARISC,
    VPP500,
    SPARC32PLUS,
    Intel960,
    PPC,
    PPC64,
    S390,
    SPU,
    V800,
    FR20,
    RH32,
    RCE,
    ARM,
    FAKE_ALPHA,
    SH,
    SPARCV9,
    TRICORE,
    ARC,
    H8_300,
    H8_300H,
    H8S,
    H8_500,
    IA_64,
    MIPS_X,
    COLDFIRE,
    Motorola68HC12,
    MMA,
    PCP,
    NCPU,
    NDR1,
    STARCORE,
    ME16,
    ST100,
    TINYJ,
    X86_64,
    PDSP,
    PDP10,
    PDP11,
    FX66,
    ST9PLUS,
    ST7,
    Motorola68HC16,
    Motorola68HC11,
    Motorola68HC08,
    Motorola68HC05,
    SVX,
    ST19,
    VAX,
    CRIS,
    JAVELIN,
    FIREPATH,
    ZSP,
    MMIX,
    HUANY,
    PRISM,
    AVR,
    FR30,
    D10V,
    D30V,
    V850,
    M32R,
    MN10300,
    MN10200,
    PJ,
    OPENRISC,
    ARC_COMPACT,
    XTENSA,
    VIDEOCORE,
    TMM_GPP,
    NS32K,
    TPC,
    SNP1K,
    ST200,
    IP2K,
    MAX,
    CR,
    F2MC16,
    MSP430,
    BLACKFIN,
    SE_C33,
    SEP,
    ARCA,
    UNICORE,
    EXCESS,
    DXP,
    ALTERA_NIOS2,
    CRX,
    XGATE,
    C166,
    M16C,
    DSPIC30F,
    CE,
    M32C,
    TSK3000,
    RS08,
    SHARC,
    ECOG2,
    SCORE7,
    DSP24,
    VIDEOCORE3,
    LATTICEMICO32,
    SE_C17,
    TI_C6000,
    TI_C2000,
    TI_C5500,
    TI_ARP32,
    TI_PRU,
    MMDSP_PLUS,
    CYPRESS_M8C,
    R32C,
    TRIMEDIA,
    QDSP6,
    Intel8051,
    STXP7X,
    NDS32,
    ECOG1X,
    MAXQ30,
    XIMO16,
    MANIK,
    CRAYNV2,
    RX,
    METAG,
    MCST_ELBRUS,
    ECOG16,
    CR16,
    ETPU,
    SLE9X,
    L10M,
    K10M,
    AARCH64,
    AVR32,
    STM8,
    TILE64,
    TILEPRO,
    MICROBLAZE,
    CUDA,
    TILEGX,
    CLOUDSHIELD,
    COREA_1ST,
    COREA_2ND,
    ARC_COMPACT2,
    OPEN8,
    RL78,
    VIDEOCORE5,
    Renesas78KOR,
    Freescale56800EX,
    BA1,
    BA2,
    XCORE,
    MCHP_PIC,
    KM32,
    KMX32,
    EMX16,
    EMX8,
    KVARC,
    CDP,
    COGE,
    COOL,
    NORC,
    CSR_KALIMBA,
    Z80,
    VISIUM,
    FT32,
    MOXIE,
    AMDGPU,
    RISCV,
    BPF,
    CSKY,
    NUM,
}

impl Machine {
    pub fn from_word(word: u16) -> Machine {
        match word {
            1 => Self::M32,
            2 => Self::SPARC,
            3 => Self::Intel386,
            4 => Self::Motorola68K,
            5 => Self::Motorola88K,
            6 => Self::IAMCU,
            7 => Self::Intel860,
            8 => Self::MIPS,
            9 => Self::S370,
            10 => Self::MIPS_RS3_LE,
            15 => Self::PARISC,
            17 => Self::VPP500,
            18 => Self::SPARC32PLUS,
            19 => Self::Intel960,
            20 => Self::PPC,
            21 => Self::PPC64,
            22 => Self::S390,
            23 => Self::SPU,
            36 => Self::V800,
            37 => Self::FR20,
            38 => Self::RH32,
            39 => Self::RCE,
            40 => Self::ARM,
            41 => Self::FAKE_ALPHA,
            42 => Self::SH,
            43 => Self::SPARCV9,
            44 => Self::TRICORE,
            45 => Self::ARC,
            46 => Self::H8_300,
            47 => Self::H8_300H,
            48 => Self::H8S,
            49 => Self::H8_500,
            50 => Self::IA_64,
            51 => Self::MIPS_X,
            52 => Self::COLDFIRE,
            53 => Self::Motorola68HC12,
            54 => Self::MMA,
            55 => Self::PCP,
            56 => Self::NCPU,
            57 => Self::NDR1,
            58 => Self::STARCORE,
            59 => Self::ME16,
            60 => Self::ST100,
            61 => Self::TINYJ,
            62 => Self::X86_64,
            63 => Self::PDSP,
            64 => Self::PDP10,
            65 => Self::PDP11,
            66 => Self::FX66,
            67 => Self::ST9PLUS,
            68 => Self::ST7,
            69 => Self::Motorola68HC16,
            70 => Self::Motorola68HC11,
            71 => Self::Motorola68HC08,
            72 => Self::Motorola68HC05,
            73 => Self::SVX,
            74 => Self::ST19,
            75 => Self::VAX,
            76 => Self::CRIS,
            77 => Self::JAVELIN,
            78 => Self::FIREPATH,
            79 => Self::ZSP,
            80 => Self::MMIX,
            81 => Self::HUANY,
            82 => Self::PRISM,
            83 => Self::AVR,
            84 => Self::FR30,
            85 => Self::D10V,
            86 => Self::D30V,
            87 => Self::V850,
            88 => Self::M32R,
            89 => Self::MN10300,
            90 => Self::MN10200,
            91 => Self::PJ,
            92 => Self::OPENRISC,
            93 => Self::ARC_COMPACT,
            94 => Self::XTENSA,
            95 => Self::VIDEOCORE,
            96 => Self::TMM_GPP,
            97 => Self::NS32K,
            98 => Self::TPC,
            99 => Self::SNP1K,
            100 => Self::ST200,
            101 => Self::IP2K,
            102 => Self::MAX,
            103 => Self::CR,
            104 => Self::F2MC16,
            105 => Self::MSP430,
            106 => Self::BLACKFIN,
            107 => Self::SE_C33,
            108 => Self::SEP,
            109 => Self::ARCA,
            110 => Self::UNICORE,
            111 => Self::EXCESS,
            112 => Self::DXP,
            113 => Self::ALTERA_NIOS2,
            114 => Self::CRX,
            115 => Self::XGATE,
            116 => Self::C166,
            117 => Self::M16C,
            118 => Self::DSPIC30F,
            119 => Self::CE,
            120 => Self::M32C,
            131 => Self::TSK3000,
            132 => Self::RS08,
            133 => Self::SHARC,
            134 => Self::ECOG2,
            135 => Self::SCORE7,
            136 => Self::DSP24,
            137 => Self::VIDEOCORE3,
            138 => Self::LATTICEMICO32,
            139 => Self::SE_C17,
            140 => Self::TI_C6000,
            141 => Self::TI_C2000,
            142 => Self::TI_C5500,
            143 => Self::TI_ARP32,
            144 => Self::TI_PRU,
            160 => Self::MMDSP_PLUS,
            161 => Self::CYPRESS_M8C,
            162 => Self::R32C,
            163 => Self::TRIMEDIA,
            164 => Self::QDSP6,
            165 => Self::Intel8051,
            166 => Self::STXP7X,
            167 => Self::NDS32,
            168 => Self::ECOG1X,
            169 => Self::MAXQ30,
            170 => Self::XIMO16,
            171 => Self::MANIK,
            172 => Self::CRAYNV2,
            173 => Self::RX,
            174 => Self::METAG,
            175 => Self::MCST_ELBRUS,
            176 => Self::ECOG16,
            177 => Self::CR16,
            178 => Self::ETPU,
            179 => Self::SLE9X,
            180 => Self::L10M,
            181 => Self::K10M,
            183 => Self::AARCH64,
            185 => Self::AVR32,
            186 => Self::STM8,
            187 => Self::TILE64,
            188 => Self::TILEPRO,
            189 => Self::MICROBLAZE,
            190 => Self::CUDA,
            191 => Self::TILEGX,
            192 => Self::CLOUDSHIELD,
            193 => Self::COREA_1ST,
            194 => Self::COREA_2ND,
            195 => Self::ARC_COMPACT2,
            196 => Self::OPEN8,
            197 => Self::RL78,
            198 => Self::VIDEOCORE5,
            199 => Self::Renesas78KOR,
            200 => Self::Freescale56800EX,
            201 => Self::BA1,
            202 => Self::BA2,
            203 => Self::XCORE,
            204 => Self::MCHP_PIC,
            210 => Self::KM32,
            211 => Self::KMX32,
            212 => Self::EMX16,
            213 => Self::EMX8,
            214 => Self::KVARC,
            215 => Self::CDP,
            216 => Self::COGE,
            217 => Self::COOL,
            218 => Self::NORC,
            219 => Self::CSR_KALIMBA,
            220 => Self::Z80,
            221 => Self::VISIUM,
            222 => Self::FT32,
            223 => Self::MOXIE,
            224 => Self::AMDGPU,
            243 => Self::RISCV,
            247 => Self::BPF,
            252 => Self::CSKY,
            253 => Self::NUM,
            _ => Self::NONE,
        }
    }
}

impl ObjectType {
    pub fn from_word(word: u16) -> ObjectType {
        match word {
            1 => Self::Relocatable,
            2 => Self::Executable,
            3 => Self::SharedObject,
            4 => Self::Core,
            0xff00 => Self::LOProcessorSpecific,
            0xffff => Self::HIProcessorSpecific,
            _ => Self::Unknown,
        }
    }
}

impl OperatingSystemABI {
    pub fn from_byte(byte: u8) -> OperatingSystemABI {
        match byte {
            0 => Self::NONE_OR_SYSV,
            1 => Self::HPUX,
            2 => Self::NETBSD,
            3 => Self::GNU_LINUX,
            6 => Self::SOLARIS,
            7 => Self::AIX,
            8 => Self::IRIX,
            9 => Self::FREEBSD,
            10 => Self::TRU64,
            11 => Self::MODESTO,
            12 => Self::OPENBSD,
            64 => Self::ARM_E,
            97 => Self::ARM,
            255 => Self::STANDALONE,
            _ => Self::UNKNOWN,
        }
    }
}

impl Version {
    pub fn from_byte(byte: u8) -> Version {
        match byte {
            0 => Version::Invalid,
            _ => Version::Current,
        }
    }

    pub fn from_u32(data: u32) -> Version {
        match data {
            0 => Version::Invalid,
            _ => Version::Current,
        }
    }
}

impl Class {
    #[inline]
    pub fn from_byte(byte: u8) -> Class {
        match byte {
            1 => Class::ELF32,
            2 => Class::ELF64,
            _ => Class::Invalid,
        }
    }
}

impl DataEncoding {
    #[inline]
    pub fn from_byte(byte: u8) -> DataEncoding {
        match byte {
            1 => Self::LSB,
            2 => Self::MSB,
            _ => Self::Unknown,
        }
    }
}

pub fn parse_eident(
    data: &[u8],
) -> MidasSysResult<(Class, DataEncoding, Version, OperatingSystemABI)> {
    if data[0..4] != MidasELFHeader::MAGIC {
        return Err(format!(
            "ELF Magic not found; binary blob possibly not in ELF format? ({:?})",
            &data[..4]
        ));
    }
    let arch = Class::from_byte(data[4]);
    let encoding = DataEncoding::from_byte(data[5]);
    let version = Version::from_byte(data[6]);
    let osabi = OperatingSystemABI::from_byte(data[7]);
    Ok((arch, encoding, version, osabi))
}

#[allow(unused)]
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct MidasELFHeader {
    pub architecture: Class,
    pub encoding: DataEncoding,
    pub elf_version: Version,
    pub os_abi: OperatingSystemABI,
    pub object_type: ObjectType,
    pub machine_type: Machine,
    pub file_version: Version,
    pub entry_point_addr: usize,
    pub program_header_offset: u64,
    pub section_header_offset: u64,
    pub flags: u32,
    pub elf_header_size: u16,
    pub program_header_entry_size: u16,
    pub program_header_entries: u16,
    pub section_header_entry_size: u16,
    pub section_header_entries: u16,
    pub section_header_string_index: u16,
}

impl MidasELFHeader {
    pub const MAGIC: [u8; 4] = [0x7F, 0x45, 0x4C, 0x46];
    pub fn from(bytes: &[u8]) -> MidasSysResult<MidasELFHeader> {
        use crate::utils::unchecked;
        let (architecture, encoding, elf_version, os_abi) = parse_eident(&bytes[0..16])?;
        let object_type = ObjectType::from_word(unsafe { unchecked::bytes_to_u16(&bytes[16..18]) });
        let machine_type = Machine::from_word(unsafe { unchecked::bytes_to_u16(&bytes[18..20]) });
        let file_version = Version::from_u32(unsafe { unchecked::bytes_to_u32(&bytes[20..24]) });
        let entry_point_addr = unsafe { unchecked::bytes_to_u64(&bytes[24..32]) } as usize;
        let program_header_offset = unsafe { unchecked::bytes_to_u64(&bytes[32..40]) };
        let section_header_offset = unsafe { unchecked::bytes_to_u64(&bytes[40..48]) };
        let flags = unsafe { unchecked::bytes_to_u32(&bytes[48..52]) };
        let elf_header_size = unsafe { unchecked::bytes_to_u16(&bytes[52..54]) };
        let program_header_entry_size = unsafe { unchecked::bytes_to_u16(&bytes[54..56]) };
        let program_header_entries = unsafe { unchecked::bytes_to_u16(&bytes[56..58]) };
        let section_header_entry_size = unsafe { unchecked::bytes_to_u16(&bytes[58..60]) };
        let section_header_entries = unsafe { unchecked::bytes_to_u16(&bytes[60..62]) };
        let section_header_string_index = unsafe { unchecked::bytes_to_u16(&bytes[62..64]) };

        Ok(MidasELFHeader {
            architecture,
            encoding,
            elf_version,
            os_abi,
            object_type,
            machine_type,
            file_version,
            entry_point_addr,
            program_header_offset,
            section_header_offset,
            flags,
            elf_header_size,
            program_header_entry_size,
            program_header_entries,
            section_header_entry_size,
            section_header_entries,
            section_header_string_index,
        })
    }
}

impl std::fmt::Display for MidasELFHeader {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", format_args!("ELF Header\nArchitecture class: {:?}\nData encoding: {:?}\nELF Version: {:?}\nOS ABI: {:?}\nObject Type: {:?}\nMachine type: {:?}\nFile version: {:?}\nEntry address: 0x{:X}\nProgram Header Offset: {} bytes\nSection Header Offset: {} bytes\nFlags value: {}\nELF Header size: {} bytes\nProgram Header Entry Size: {} bytes\nProgram Header Entries: {} entries\nSection Header Entry Size: {} bytes\nSection Header Entries: {} entries\nSection Header String Index: {}\n",
        &self.architecture, &self.encoding, &self.elf_version, &self.os_abi, &self.object_type, &self.machine_type, &self.file_version, &self.entry_point_addr, &self.program_header_offset, &self.section_header_offset, &self.flags, &self.elf_header_size, &self.program_header_entry_size, &self.program_header_entries, &self.section_header_entry_size, &self.section_header_entries, &self.section_header_string_index))
    }
}
