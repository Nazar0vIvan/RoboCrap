#pragma once

#include <QtGlobal>

#include <chrono>

// Nanoseconds of std::chrono::steady_clock: the one time base of every record row
// (RsiDevice, PlcDevice and the application's session recorder; docs/code context/RECORDS_FORMAT.md).
inline qint64 recordClockNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
