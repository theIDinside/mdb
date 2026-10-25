/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/typedefs.h>

// std
#include <format>
#include <string>
#include <string_view>

namespace mdb::views {

struct SourceLoc
{
  u32 mLine{ 0 };
  u32 mColumn{ 0 };
};

struct Diagnostic
{
  std::string mFile;
  SourceLoc mLoc;
  std::string mMessage;

  [[nodiscard]] std::string
  ToString() const
  {
    return std::format("{}:{}:{}: {}", mFile, mLoc.mLine, mLoc.mColumn, mMessage);
  }
};

} // namespace mdb::views
