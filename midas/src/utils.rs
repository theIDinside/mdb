/// Takes a to_string'able type and to_string's it. Utility function, for shortening the code
/// that for instance, returns a Result<T, SomeErrorThatIsNotCompatibleWithMidasSysResult::Error> and converts it to our Error format (which currently is a String only, but in the future will change)
#[inline(always)]
pub fn midas_err<T: ToString>(to_stringable_error: T) -> String {
    to_stringable_error.to_string()
}
