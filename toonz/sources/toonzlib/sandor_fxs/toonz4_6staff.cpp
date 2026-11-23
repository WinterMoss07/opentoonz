

#include "toonz4_6staff.h"
#include "trop.h"
#include "tconvert.h"
#include "toonz4.6/toonz.h"

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif
//---------------------------------------------------------------------

//-----------------------------------------------------------------------

const char *strsave(const char *t) {
  char *s;
  s = (char *)malloc(strlen(t) + 1);
  strcpy(s, t);
  return s;
}

//-----------------------------------------------------------------------

void convertParam(double param[], const char *cParam[], int cParamLen) {
  std::string app;
  for (int i = 1; i < 12; i++) {
    app       = std::to_string(param[i]);
    cParam[i] = strsave(app.c_str());
  }
}

//-----------------------------------------------------------------------
