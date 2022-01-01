use std::sync::Once;

static ErrNo: Once = Once::new();
static mut ERRNO: *mut i32 = std::ptr::null_mut() as _;

fn get_errno() -> &'static i32 {
    ErrNo.call_once(|| {
        unsafe {
            ERRNO = libc::__errno_location();
            if ERRNO.is_null() {
                panic!("failed to get errno location");
            }
        };
    });
    unsafe { &*ERRNO }
}

pub fn get_errno_msg() -> String {
    unsafe {
        let errno = get_errno();
        let err_msg = libc::strerror(*errno);
        let err = std::ffi::CString::from_raw(err_msg);
        if err.as_bytes().is_empty() {
            return "No errno message found".into();
        }
        err.to_str().unwrap().to_string()
    }
}
