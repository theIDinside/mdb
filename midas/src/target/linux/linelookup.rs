use std::{
    io::Read,
    path::{Path, PathBuf},
    str::Lines,
};

use crate::{MidasError, MidasSysResult};

pub struct ReadFile {
    contents: String,
    path: PathBuf,
}

impl ReadFile {
    pub fn new(contents: String, path: PathBuf) -> ReadFile {
        ReadFile { contents, path }
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn text(&self) -> &String {
        &self.contents
    }
}

pub struct ReadFileRingBuffer {
    fifo_filecontents: [Option<ReadFile>; 5],
    size: usize,
}

impl ReadFileRingBuffer {
    pub fn new() -> ReadFileRingBuffer {
        ReadFileRingBuffer {
            fifo_filecontents: [None, None, None, None, None],
            size: 0,
        }
    }

    /// Reads the file at `path` and caches it in this ring buffer.
    /// * `path` - the full path to the file to be read & cached
    /// * `returns` the cached `ReadFile`
    pub fn cache(&mut self, path: &Path) -> MidasSysResult<&ReadFile> {
        let p = self
            .fifo_filecontents
            .iter()
            .position(|f| f.as_ref().map(|f| f.path() == path).unwrap_or(false));

        if let Some(pos) = p {
            Ok(self.fifo_filecontents[pos].as_ref().unwrap())
        } else {
            let mut f = std::fs::File::open(path).map_err(|e| MidasError::FileOpenError(e.kind()))?;
            let mut buf = String::with_capacity(
                f.metadata()
                    .map_err(|e| MidasError::FileOpenError(e.kind()))?
                    .len() as usize,
            );
            let bytes = f
                .read_to_string(&mut buf)
                .map_err(|e| MidasError::FileReadError(e.kind()))?;
            let read_file = ReadFile::new(buf, path.to_path_buf());
            self.insert(read_file);
            Ok(self.get_newest().unwrap())
        }
    }

    pub fn insert(&mut self, read_file: ReadFile) {
        if self.size == self.fifo_filecontents.len() {
            self.fifo_filecontents[0] = None;
            for x in 0..self.fifo_filecontents.len() - 1 {
                self.fifo_filecontents.swap(x, x + 1);
            }
            self.fifo_filecontents[self.fifo_filecontents.len() - 1] = Some(read_file);
        } else {
            self.fifo_filecontents[self.size] = Some(read_file);
            self.size += 1;
        }
    }

    pub fn get_newest(&self) -> Option<&ReadFile> {
        self.fifo_filecontents
            .iter()
            .rev()
            .find(|d| d.is_some())
            .and_then(|f| f.as_ref())
    }

    pub fn get_if_cached(&mut self, path: &Path) -> Option<&ReadFile> {
        self.fifo_filecontents
            .iter()
            .filter_map(|f| f.as_ref())
            .find(|&f| f.path() == path)
    }
}
