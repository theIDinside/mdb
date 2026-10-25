/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <views/ast.h>

// std
#include <memory>
#include <string>

namespace mdb::views {

/**
 * Parses a view file (.mdbv). Never fails as a whole: views that contain errors are dropped and reported in
 * `ViewFile::mDiagnostics`, the rest of the file is still usable.
 *
 *   view nsTArray<.*> in "libxul.so" {
 *     using T = template_arg(0)
 *     let hdr = mHdr;
 *     summary "length={hdr->mLength}"
 *     children array((T*)(hdr + 1), hdr->mLength)
 *   }
 */
std::unique_ptr<ViewFile> ParseViewFile(std::string path, std::string source) noexcept;

// Keywords that start a builtin type in a cast or sizeof: `unsigned`, `char16_t`, `u32`, ...
bool IsBuiltinTypeKeyword(std::string_view word) noexcept;

} // namespace mdb::views
