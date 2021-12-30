pub enum KeyboardInput {
    Char(char),
    Left,
    Right,
    Up,
    Down,
    PageUp,
    PageDown,
    Home,
    End,
    Enter,
    Escape,
    Delete,
    Backspace,
    Err,
}

impl Into<ParsedKey> for KeyboardInput {
    fn into(self) -> ParsedKey {
        ParsedKey {
            key: self,
            modifier: None,
        }
    }
}

pub enum Modifier {
    Control,
    Shift,
    Alt,
}

// For our intents and purposes, we only care about when 1 modifier is active. in other solutions, we would *obviously* care
// if several modifiers are in effect.
pub(crate) struct ParsedKey {
    pub key: KeyboardInput,
    pub modifier: Option<Modifier>,
}

pub(crate) fn read_key() -> ParsedKey {
    unsafe {
        use libc::read;
        let mut buf = [0u8; 6];
        let mut bytes_read = 0;
        while bytes_read == 0 {
            bytes_read = read(0, buf.as_mut_ptr() as _, 1);
            if bytes_read == -1 {
                panic!("CLI ERROR: Read from STDIN failed");
            }
        }
        match &buf[0] {
            8 => ParsedKey {
                key: KeyboardInput::Backspace,
                modifier: Some(Modifier::Control),
            },
            27 => {
                if read(0, buf.as_mut_ptr().offset(1) as _, 2) == 0 {
                    return KeyboardInput::Escape.into();
                }
                if buf[1] as char == '[' {
                    if buf[2] >= '0' as u8 && buf[2] <= '9' as u8 {
                        // extended escape seq
                        if read(0, buf.as_mut_ptr().offset(3) as _, 1) == 0 {
                            return KeyboardInput::Escape.into();
                        }
                        let k = buf[2] as char;
                        if buf[3] as char == '~' {
                            match k {
                                '3' => return KeyboardInput::Delete.into(),
                                '5' => return KeyboardInput::PageUp.into(),
                                '6' => return KeyboardInput::PageDown.into(),
                                _ => {}
                            }
                        } else if buf[3] as char == ';' {
                            if read(0, buf.as_mut_ptr().offset(4) as _, 2) == -1 {
                                panic!("failed to read from STDIN");
                            }
                            if buf[4] as char == '5' {
                                return match buf[5] as char {
                                    'A' => ParsedKey {
                                        key: KeyboardInput::Up,
                                        modifier: Some(Modifier::Control),
                                    },
                                    'B' => ParsedKey {
                                        key: KeyboardInput::Down,
                                        modifier: Some(Modifier::Control),
                                    },
                                    'C' => ParsedKey {
                                        key: KeyboardInput::Right,
                                        modifier: Some(Modifier::Control),
                                    },
                                    'D' => ParsedKey {
                                        key: KeyboardInput::Left,
                                        modifier: Some(Modifier::Control),
                                    },
                                    _ => ParsedKey {
                                        key: KeyboardInput::Err,
                                        modifier: None,
                                    },
                                };
                            }
                        }
                    } else {
                        return match buf[2] as char {
                            'A' => KeyboardInput::Up.into(),
                            'B' => KeyboardInput::Down.into(),
                            'C' => KeyboardInput::Right.into(),
                            'D' => KeyboardInput::Left.into(),
                            'H' => KeyboardInput::Home.into(),
                            'F' => KeyboardInput::End.into(),
                            _ => KeyboardInput::Err.into(),
                        };
                    }
                } else if buf[1] as char == 'O' {
                    return match buf[2] as char {
                        'H' => KeyboardInput::Home.into(),
                        'F' => KeyboardInput::End.into(),
                        _ => KeyboardInput::Err.into(),
                    };
                }
                return KeyboardInput::Err.into();
            }
            127 => KeyboardInput::Backspace.into(),
            13 => KeyboardInput::Enter.into(),
            _ => KeyboardInput::Char(buf[0] as char).into(),
        }
    }
}
