#pragma once

#ifndef TSCANNER_H
#define TSCANNER_H

#include "trasterimage.h"
#include <set>
#include <QString>
#undef DVAPI
#undef DVVAR
#ifdef TNZBASE_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

// forward declaration
class TOStream;
class TIStream;
class TFilePath;

struct DVAPI TScanParam {
  TScanParam()
      : m_supported(false)
      , m_min(0)
      , m_max(0)
      , m_def(0)
      , m_step(0)
      , m_value(0) {}

  TScanParam(float _min, float _max, float _def, float _step)
      : m_supported(true)
      , m_min(_min)
      , m_max(_max)
      , m_def(_def)
      , m_step(1)
      , m_value(_def) {}

  bool m_supported;
  float m_min, m_max, m_def, m_step,
      m_value;  // questi sono float per seguire lo standard TWAIN !

  void update(const TScanParam &model);
  // assegna min,max,def,step e supported da model a this
  // mantiene value a patto che sia contenuto nel nuovo range, diversamente ne
  // fa il crop
};

class DVAPI TScannerParameters {
public:
  enum ScanType { None, BW, GR8, RGB24 };

private:
  // Supported scan types: Black and white, graytones, color
  bool m_bw, m_gray, m_rgb;

  // Current scan type
  ScanType m_scanType;

  std::string m_paperFormat;  // e.g. "A4 paper"
  TRectD m_scanArea;  // in mm /* TWAIN preferirebbe gli inch, ma uso i mm per
                      // seguire tnz4.x*/
  TRectD m_cropBox;   // in mm /* TWAIN preferirebbe gli inch, ma uso i mm per
                      // seguire tnz4.x*/
  bool m_isPreview;
  TDimensionD m_maxPaperSize;  // in mm /* TWAIN preferirebbe gli inch, ma uso i
                               // mm per seguire tnz4.x*/

  bool m_paperOverflow;  // vale true se la scanArea e' stata tagliata rispetto
                         // alle dimensioni della carta
                         // per rispettare la maxPaperSize

public:
  TScanParam m_brightness;
  TScanParam m_contrast;
  TScanParam m_threshold;
  TScanParam m_dpi;
  TScanParam m_paperFeeder;  // value==1.0 => use paper feeder

private:
  // other useful info ?!
  std::string m_twainVersion;
  std::string m_manufacturer;
  std::string m_prodFamily;
  std::string m_productName;
  std::string m_version;

  bool m_reverseOrder;  // if true then scan levels starting from last frame

  void cropScanArea();  // assicura che scanArea sia dentro maxPaperSize

public:
  TScannerParameters();
  ~TScannerParameters();

  bool isSupported(ScanType) const;


  void setPaperFormat(std::string paperFormat);
  // assert(TPaperFormatManager::instance()->isValidFormat(paperFormat));
  // updates scanArea (cropping with maxPaperSize)

  // if paperFormat=="" set default paper format; recompute (to be sure!)
  // scanArea


  bool isPreview() const { return m_isPreview; }




  void assign(const TScannerParameters *params);
  void saveData(TOStream &os) const;
  void loadData(TIStream &is);
};

//---------------------------------------------------------

class DVAPI TPaperFormatManager {  // singleton
public:
  class Format {
  public:
    TDimensionD m_size;
    Format() : m_size(0, 0) {}
    Format(const TDimensionD &size) : m_size(size) {}
  };

private:
  typedef std::map<std::string, Format> FormatTable;
  FormatTable m_formats;

  TPaperFormatManager();
  void readPaperFormat(const TFilePath &fp);
  void readPaperFormats();

public:
  static TPaperFormatManager *instance();


  bool isValidFormat(std::string name) const;
  std::string getDefaultFormat() const;

  // nome formato --> dimensione
  TDimensionD getSize(std::string name) const;
};

#endif
