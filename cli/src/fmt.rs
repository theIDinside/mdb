#[derive(Clone, Copy)]
pub enum Style {
    Bold,
    Underlined,
}
#[derive(Clone, Copy)]
pub enum TextColor {
    Red,
    Green,
}
#[derive(Clone, Copy)]
pub struct Format {
    color: Option<TextColor>,
    style: Option<Style>,
}

impl Format {
    pub fn new() -> Format {
        Format {
            color: None,
            style: None,
        }
    }

    pub fn new_with(style: Style, color: TextColor) -> Format {
        Format {
            color: Some(color),
            style: Some(style),
        }
    }

    pub fn style(mut self, style: Style) -> Self {
        self.style = Some(style);
        self
    }

    pub fn color(mut self, color: TextColor) -> Self {
        self.color = Some(color);
        self
    }

    pub fn make(&self) -> Vec<u8> {
        let mut v: Vec<u8> = Vec::with_capacity("\x1b[31\x1b[31".len());
        v.extend_from_slice(
            &self
                .color
                .as_ref()
                .map(|c| Into::<&str>::into(c).as_bytes())
                .unwrap_or("".as_bytes()),
        );
        v.extend_from_slice(
            self.style
                .as_ref()
                .map(|s| Into::<&str>::into(s).as_bytes())
                .unwrap_or("".as_bytes()),
        );
        v
    }
}

impl Into<&'static str> for Style {
    fn into(self) -> &'static str {
        match self {
            Style::Bold => "\x1b[1m",
            Style::Underlined => "\x1b[4m",
        }
    }
}

impl Into<&'static str> for TextColor {
    fn into(self) -> &'static str {
        match self {
            TextColor::Red => "\x1b[31m",
            TextColor::Green => "\x1b[32m",
        }
    }
}

impl Into<&'static str> for &Style {
    fn into(self) -> &'static str {
        match self {
            Style::Bold => "\x1b[1m",
            Style::Underlined => "\x1b[4m",
        }
    }
}

impl Into<&'static str> for &TextColor {
    fn into(self) -> &'static str {
        match self {
            TextColor::Red => "\x1b[31m",
            TextColor::Green => "\x1b[32m",
        }
    }
}

pub struct FormattedBuffer {
    pub output: Vec<u8>,
}

impl FormattedBuffer {
    pub fn new() -> FormattedBuffer {
        FormattedBuffer { output: vec![] }
    }

    pub fn with_capacity(capacity: usize) -> FormattedBuffer {
        FormattedBuffer {
            output: Vec::with_capacity(capacity),
        }
    }

    pub fn add_unformatted<S: AsRef<str>>(&mut self, data: S) {
        self.output.extend_from_slice(data.as_ref().as_bytes());
    }

    pub fn add_formatted<S: AsRef<str>>(&mut self, string: S, fmt: Format) {
        self.output.extend_from_slice(&fmt.make());
        self.output.extend_from_slice(string.as_ref().as_bytes());
        self.output.extend_from_slice("\x1b[00m".as_bytes());
    }

    pub fn add_formatted_line<S: AsRef<str>>(&mut self, string: S, fmt: Format) {
        self.add_formatted(string, fmt);
        self.add_unformatted("\r\n");
    }
}
