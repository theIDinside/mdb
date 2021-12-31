extern crate linuxwrapper as nixwrap;

pub mod commands;
pub mod software_breakpoint;
pub mod target;
pub mod types;

pub type MidasSysResult<T> = Result<T, String>;

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        let result = 2 + 2;
        assert_eq!(result, 4);
    }
}
