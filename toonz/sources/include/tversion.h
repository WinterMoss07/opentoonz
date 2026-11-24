#pragma once

#ifndef TVER_INCLUDED
#define TVER_INCLUDED

#include <string>

namespace TVER {

// Version of this fork: year.month.release (26.11.1 = first release of
// November 2026), based on OpenToonz 1.8
class ToonzVersion {
public:
  std::string getAppName(void) { return applicationName; }
  std::string getSystemVarPrefix(void) { return systemVarPrefix; }
  std::string getAppVersionString(void) { return applicationVersion; }
  std::string getAppVersionInfo(std::string msg) {
    return std::string(applicationName) + " " + msg + " v" +
           applicationVersion;
  }

private:
  const char *applicationName    = "OpenToonz";
  const char *applicationVersion = "26.11.1";
  const char *systemVarPrefix    = "TOONZ";
};

}  // namespace TVER

#endif  // TVER_INCLUDED
