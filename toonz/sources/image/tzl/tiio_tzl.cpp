

#include "tiio_tzl.h"
#include "tmachine.h"
#include "tsystem.h"
#include "ttoonzimage.h"
#include "tstream.h"
#include "tfilepath_io.h"
#include "tpixelcm.h"
#include "tpalette.h"
#include "tcodec.h"
#include <stdio.h>
#include "trastercm.h"
#include "timageinfo.h"
#include "timagecache.h"
#include "tcontenthistory.h"
#include "tropcm.h"
#include "tenv.h"
#include "tconvert.h"
#include "trasterimage.h"

#include <QByteArray>

#include <algorithm>
#include <vector>

#if !defined(TNZ_LITTLE_ENDIAN)
TNZ_LITTLE_ENDIAN undefined !!
#endif

    const int CREATOR_LENGTH = 40;

namespace {

char *reverse(char *buffer, int size) {
  char *start = buffer;
  char *end   = start + size;
  while (start < --end) {
    char tmp = *start;
    *start++ = *end;
    *end     = tmp;
  }
  return buffer;
}

// switch the saving version according to the file path property
int currentVersion() {
  if (TFilePath::useStandard()) return 14;
  return 15;
}

}  // namespace

static int tfwrite(const char *data, const unsigned int count, FILE *f) {
  return fwrite(data, sizeof(char), count, f);
}

static int tfwrite(UCHAR *data, const unsigned int count, FILE *f) {
  return fwrite(data, sizeof(char), count, f);
}

static int tfwrite(TINT32 *data, const unsigned int count, FILE *f) {
  if (count == 1) {
#if !TNZ_LITTLE_ENDIAN
    TUINT32 dataToWrite = swapTINT32(*data);
#else
    TUINT32 dataToWrite = *data;
#endif
    return fwrite(&dataToWrite, sizeof(TINT32), 1, f);
  }
  assert(0);
  return 0;
}

static int tfwrite(TUINT32 *data, const unsigned int count, FILE *f) {
  if (count == 1) {
#if !TNZ_LITTLE_ENDIAN
    TUINT32 dataToWrite = swapTINT32(*data);
#else
    TUINT32 dataToWrite = *data;
#endif
    return fwrite(&dataToWrite, sizeof(TINT32), 1, f);
  }
  assert(0);
  return 0;
}

static int tfwrite(double *data, unsigned int count, FILE *f) {
  if (count == 1) {
    double v  = *data;
    char *ptr = (char *)&v;
#if !TNZ_LITTLE_ENDIAN
    ptr = reverse((char *)&v, sizeof(double));
#endif
    return fwrite(ptr, sizeof(double), 1, f);
  }
  assert(0);
  return 0;
}

//===================================================================
//
// TImageReaderTzl
//
//-------------------------------------------------------------------

namespace {

bool erasedFrame;  // True if at least one frame has been removed.

bool writeVersionAndCreator(FILE *chan, const char *version, QString creator) {
  if (!chan) return false;
  tfwrite(version, strlen(version), chan);

  // creator : CREATOR_LENGTH character
  char s[CREATOR_LENGTH];
  if (creator.length() == 0) creator = "UNKNOWN";
  memset(s, 0, sizeof s);
  if (creator.length() > CREATOR_LENGTH - 1)
    memcpy(s, creator.toLatin1(), CREATOR_LENGTH - 1);
  else
    memcpy(s, creator.toLatin1(), creator.length());
  tfwrite(s, CREATOR_LENGTH, chan);

  return true;
}

// Bytes before the first frame: magic, creator, header size, lx, ly, frame
// count, the two table positions and the codec name
const TINT32 HEADER_END = 8 + CREATOR_LENGTH + 6 * sizeof(TINT32) + 4;
// Bytes before a frame's compressed data: savebox, buffer size and dpi
const TINT32 FRAME_FIELDS_SIZE = 5 * sizeof(TINT32) + 2 * sizeof(double);
// Bytes before an icon's compressed data: icon size and buffer size
const TINT32 ICON_FIELDS_SIZE = 3 * sizeof(TINT32);
// Frame suffixes are short ("a", "b"...); anything longer is corruption
const TINT32 MAX_SUFFIX_LENGTH = 256;

template <class T>
bool readValue(FILE *chan, T &value) {
  return fread(&value, sizeof(T), 1, chan) == 1;
}

//! Only TLV14 (frame letters) and TLV15 (frame suffixes) are supported:
//! both are what OpenToonz writes, depending on the project's frame format.
bool readVersion(FILE *chan, int &version) {
  char magic[8];
  if (fread(magic, sizeof(char), 8, chan) != 8) return false;
  if (memcmp(magic, "TLV14", 5) == 0)
    version = 14;
  else if (memcmp(magic, "TLV15", 5) == 0)
    version = 15;
  else
    return false;
  return true;
}

//-------------------------------------------------------------------

//! Reads one entry of the frame or icon table.
bool readTableEntry(FILE *chan, int version, TFrameId &fid, TzlChunk &chunk) {
  TINT32 number, offs, length;
  QByteArray suffix;
  if (!readValue(chan, number)) return false;
  if (version >= 15) {
    TINT32 suffixLength;
    if (!readValue(chan, suffixLength) || suffixLength < 0 ||
        suffixLength > MAX_SUFFIX_LENGTH)
      return false;
    suffix.resize(suffixLength);
    if (suffixLength &&
        fread(suffix.data(), sizeof(char), suffixLength, chan) !=
            (size_t)suffixLength)
      return false;
  } else {
    char letter;
    if (!readValue(chan, letter)) return false;
    suffix = QByteArray(&letter, 1);
  }
  if (!readValue(chan, offs) || !readValue(chan, length)) return false;
  fid   = TFrameId(number, QString::fromUtf8(suffix));
  chunk = TzlChunk(offs, length);
  return true;
}

//-------------------------------------------------------------------

/*!
  Reads the header and the frame and icon tables of a TLV file. Returns false
  when the file can't be trusted: unsupported version, truncated header or
  tables, sizes out of range, data chunks outside the data area or
  overlapping each other. A writer must never work on such a file: it would
  write over other frames.
*/
bool readHeaderAndOffsets(FILE *chan, TzlOffsetMap &frameOffsTable,
                          TzlOffsetMap &iconOffsTable, TDimension &res,
                          int &version, QString &creator, TINT32 *_frameCount,
                          TINT32 *_offsetTablePos, TINT32 *_iconOffsetTablePos,
                          TLevelP level) {
  assert(frameOffsTable.empty());
  assert(iconOffsTable.empty());

  if (fseek(chan, 0, SEEK_END)) return false;
  long fileSize = ftell(chan);
  if (fileSize < HEADER_END || fseek(chan, 0, SEEK_SET)) return false;

  if (!readVersion(chan, version)) return false;

  char buffer[CREATOR_LENGTH + 1];
  memset(buffer, 0, sizeof buffer);
  if (fread(buffer, sizeof(char), CREATOR_LENGTH, chan) != CREATOR_LENGTH)
    return false;
  creator = buffer;

  TINT32 hdrSize, lx, ly, frameCount, offsetTablePos, iconOffsetTablePos;
  char codec[4];
  if (!readValue(chan, hdrSize) || !readValue(chan, lx) ||
      !readValue(chan, ly) || !readValue(chan, frameCount) ||
      !readValue(chan, offsetTablePos) ||
      !readValue(chan, iconOffsetTablePos) || fread(codec, 4, 1, chan) != 1)
    return false;

  // A frame raster must fit the 32-bit sizes used everywhere
  if (lx <= 0 || ly <= 0 ||
      (long long)lx * ly * (long long)sizeof(TPixelCM32) > 0x7fffffffLL)
    return false;
  if (frameCount < 0 || frameCount > 60000) return false;

  // Both tables follow the frame data. Zero positions mean the file was never
  // completed (the writer fills them in last).
  if (offsetTablePos < HEADER_END || offsetTablePos > fileSize ||
      iconOffsetTablePos < offsetTablePos || iconOffsetTablePos > fileSize)
    return false;

  std::vector<TzlChunk> chunks;
  auto inDataArea = [offsetTablePos](const TzlChunk &chunk,
                                     TINT32 fieldsSize) {
    return chunk.m_offs >= HEADER_END && chunk.m_length > fieldsSize &&
           (long long)chunk.m_offs + chunk.m_length <= offsetTablePos;
  };

  if (fseek(chan, offsetTablePos, SEEK_SET)) return false;
  for (int i = 0; i < frameCount; i++) {
    TFrameId fid;
    TzlChunk chunk;
    if (!readTableEntry(chan, version, fid, chunk) ||
        !inDataArea(chunk, FRAME_FIELDS_SIZE) || frameOffsTable.count(fid))
      return false;
    frameOffsTable[fid] = chunk;
    chunks.push_back(chunk);
  }

  // The icon table ends the file. It can be shorter than the frame table when
  // a save failed after writing a frame and before its icon.
  if (fseek(chan, iconOffsetTablePos, SEEK_SET)) return false;
  for (int i = 0; i < frameCount && ftell(chan) < fileSize; i++) {
    TFrameId fid;
    TzlChunk chunk;
    if (!readTableEntry(chan, version, fid, chunk) ||
        !inDataArea(chunk, ICON_FIELDS_SIZE) || iconOffsTable.count(fid) ||
        !frameOffsTable.count(fid))
      return false;
    iconOffsTable[fid] = chunk;
    chunks.push_back(chunk);
  }

  // Overlapping chunks would let the writer reuse space still in use
  std::sort(chunks.begin(), chunks.end());
  for (size_t i = 1; i < chunks.size(); ++i)
    if ((long long)chunks[i - 1].m_offs + chunks[i - 1].m_length >
        chunks[i].m_offs)
      return false;

  if (level)
    for (const auto &frame : frameOffsTable)
      level->setFrame(frame.first, TImageP());

  res.lx = lx;
  res.ly = ly;
  if (_offsetTablePos) *_offsetTablePos = offsetTablePos;
  if (_iconOffsetTablePos) *_iconOffsetTablePos = iconOffsetTablePos;
  if (_frameCount) *_frameCount = frameCount;

  return true;
}

//-------------------------------------------------------------------

//! Replaces dst with a copy of src. dst is never left missing or half
//! written: it is moved aside first and put back if the copy fails.
bool replaceWithCopy(const TFilePath &dst, const TFilePath &src) {
  if (!TSystem::doesExistFileOrLevel(src)) return false;
  TFilePath backup = dst.getParentDir() + ("~" + dst.getName() + "backup&.tlv");
  try {
    if (TSystem::doesExistFileOrLevel(backup)) TSystem::deleteFile(backup);
    if (TSystem::doesExistFileOrLevel(dst)) TSystem::renameFile(backup, dst);
  } catch (...) {
    return false;
  }
  try {
    TSystem::copyFile(dst, src);
  } catch (...) {
    try {
      if (TSystem::doesExistFileOrLevel(dst)) TSystem::deleteFile(dst);
      if (TSystem::doesExistFileOrLevel(backup))
        TSystem::renameFile(dst, backup);
    } catch (...) {
    }
    return false;
  }
  try {
    if (TSystem::doesExistFileOrLevel(backup)) TSystem::deleteFile(backup);
  } catch (...) {
  }
  return true;
}
}  // namespace



//-------------------------------------------------------------------

class TImageReaderTzl final : public TImageReader {
public:
  TImageReaderTzl(const TFilePath &f, const TFrameId &fid, TLevelReaderTzl *);
  ~TImageReaderTzl() {}

private:
  // not implemented
  TImageReaderTzl(const TImageReaderTzl &);
  TImageReaderTzl &operator=(const TImageReaderTzl &src);
  TImageP load14();

public:
  //! Indice del frame del livello
  TFrameId m_fid;
  TImageP load() override;
  TImageP loadIcon() override {
    m_isIcon = true;
    return load();
  }
  const TImageInfo *getImageInfo() const override;

  // TImageP doLoad();

  TDimension getSize() const;
  TRect getBBox() const;

private:
  //! Size of image
  int m_lx, m_ly;
  bool m_isIcon;
  //! Reference to level reader
  TLevelReaderTzl *m_lrp;
};

//===================================================================
//
// TImageWriterTzl
//
//-------------------------------------------------------------------

class TImageWriterTzl final : public TImageWriter {
  //! Reference to level writer
  TLevelWriterTzl *m_lwp;
  TFrameId m_fid;
  TDimension
      m_iconSize;  // Dimensioni dell'iconcina salvata all'interno del file tlv
  //	In genere questo parametro viene settato come quello impostato
  // dall'utente
  // nelle preferenze.
public:
  TImageWriterTzl(TLevelWriterTzl *lw, TFrameId fid)
      : TImageWriter(TFilePath())
      , m_lwp(lw)
      , m_fid(fid)
      , m_iconSize(TDimension(80, 60)) {}
  ~TImageWriterTzl() {}

private:
  // not implemented
  TImageWriterTzl(const TImageWriterTzl &);
  TImageWriterTzl &operator=(const TImageWriterTzl &src);

public:
  void save(const TImageP &img) override { m_lwp->save(img, m_fid); }
};
/*
//===================================================================
//
// TWriterInfo
//
//-------------------------------------------------------------------


TWriterInfo *TWriterInfoTzl::create(const std::string &ext)
{
  return new TWriterInfoTzl();
}

//-------------------------------------------------------------------

TWriterInfo *TWriterInfoTzl::clone() const
{
  return new TWriterInfoTzl(*this);
}

//-------------------------------------------------------------------
*/
namespace {

class Header {
public:
  enum RasType {
    Raster32RGBM,
    Raster64RGBM,
    Raster32CM,
    RasterGR8,
    RasterUnknown
  };

  int m_lx;
  int m_ly;
  RasType m_rasType;
};

}  // namespace

//-------------------------------------------------------------------

//-------------------------------------------------------------------

void TLevelWriterTzl::buildFreeChunksTable() {
  std::set<TzlChunk> occupiedChunks;
  TzlOffsetMap::const_iterator it1 = m_frameOffsTable.begin();
  TINT32 lastOccupiedPos = 0;  // ultima posizione all'interno del file occupata
                               // dall'ultima immagine(grande o icona)

  while (it1 != m_frameOffsTable.end()) {
    occupiedChunks.insert(TzlChunk(it1->second.m_offs, it1->second.m_length));
    if (it1->second.m_offs + it1->second.m_length > lastOccupiedPos)
      lastOccupiedPos = it1->second.m_offs + it1->second.m_length - 1;
    it1++;
  }
  TzlOffsetMap::const_iterator iconIt1 = m_iconOffsTable.begin();
  while (iconIt1 != m_iconOffsTable.end()) {
    occupiedChunks.insert(
        TzlChunk(iconIt1->second.m_offs, iconIt1->second.m_length));
    if (iconIt1->second.m_offs + iconIt1->second.m_length > lastOccupiedPos)
      lastOccupiedPos = iconIt1->second.m_offs + iconIt1->second.m_length - 1;
    iconIt1++;
  }

  std::set<TzlChunk>::const_iterator it2 = occupiedChunks.begin();
  TINT32 curPos;  // prima posizione utile nel file in cui vengono memorizzati i
                  // dati relativi alle immagini
  curPos = HEADER_END;

  while (it2 != occupiedChunks.end()) {
    assert(it2->m_offs >= curPos);
    if (it2->m_offs > curPos)
      m_freeChunks.insert(TzlChunk(curPos, it2->m_offs - curPos));
    curPos = it2->m_offs + it2->m_length;
    it2++;
  }
  assert(lastOccupiedPos < m_offsetTablePos);
  if (lastOccupiedPos + 1 < m_offsetTablePos)
    m_freeChunks.insert(
        TzlChunk(lastOccupiedPos + 1, m_offsetTablePos - lastOccupiedPos));
}

//===================================================================
//
// TLevelWriterTzl
//
//-------------------------------------------------------------------

TLevelWriterTzl::TLevelWriterTzl(const TFilePath &path, TPropertyGroup *info)
    : TLevelWriter(path, info)
    , m_headerWritten(false)
    , m_creatorWritten(false)
    , m_chan(0)
    , m_frameCountPos(0)
    , m_frameCount(0)
    , m_palette(0)
    , m_res(0, 0)
    , m_exists(false)
    , m_version(currentVersion())
    , m_updatedIconsSize(false)
    , m_currentIconSize(0, 0)
    , m_iconSize(TDimension(80, 60))
    , m_userIconSize(TDimension(80, 60))
    , m_adjustRatio(false)
    , m_codec(new TRasterCodecLZO("LZO", true))
    , m_overwritePaletteFlag(true) {
  m_path        = path;
  m_palettePath = path.withNoFrame().withType("tpl");
  TFileStatus fs(path);
  m_magic     = (m_version == 14) ? "TLV14B1a" : "TLV15B1a";  // actual version
  erasedFrame = false;
  // version TLV10B1a: first version
  // version TLV11B1a: added frameIds
  // version TLV12B1a: incremental writings
  // version TLV13B1a: added thumbnails
  // version TLV14B1a: add creator string (fixed size = CREATOR_LENGTH char)
  // version TLV15B1a: support multiple suffixes

  if (fs.doesExist()) {
    // if (!fs.isWritable())
    m_chan = fopen(path, "rb+");
    /*--- 誰かが開いている、または権限が無いとき ---*/
    if (!m_chan) {
      throw TSystemException(path, "can't fopen.");
    }
    /*--- TLVファイルのヘッダが正しく読めなかった場合 ---*/
    // Unsupported or damaged file: writing in it would destroy other frames
    if (!readHeaderAndOffsets(m_chan, m_frameOffsTable, m_iconOffsTable, m_res,
                              m_version, m_creator, &m_frameCount,
                              &m_offsetTablePos, &m_iconOffsetTablePos, 0)) {
      fclose(m_chan);
      m_chan = 0;
      delete m_codec;
      m_codec = 0;
      throw TSystemException(path, "can't readHeaderAndOffsets.");
    } else {
      buildFreeChunksTable();
      m_headerWritten = true;
      m_exists        = true;
      m_frameCountPos = 8 + CREATOR_LENGTH + 3 * sizeof(TINT32);
    }
  }
  if (!m_exists) {
    TFilePath parentDir = path.getParentDir();
    if (!TFileStatus(parentDir).doesExist()) {
      try {
        TSystem::mkDir(parentDir);

      } catch (...) {
        return;
      }
    }
    m_chan = fopen(path, "wb");
    if (!m_chan) return;
    if (!writeVersionAndCreator(m_chan, m_magic, m_creator)) return;
  }
}

//-------------------------------------------------------------------

TLevelWriterTzl::~TLevelWriterTzl() {
  // Exceptions must not leave a destructor (it would end the program)
  try {
    if (m_version < currentVersion() && !convertToLatestVersion()) {
      delete m_codec;
      if (m_chan) fclose(m_chan);
      return;
    }
  } catch (...) {
    delete m_codec;
    if (m_chan) fclose(m_chan);
    return;
  }
  delete m_codec;

  TINT32 offsetMapPos     = 0;
  TINT32 iconOffsetMapPos = 0;
  if (!m_chan) return;
  // nothing was saved: there is no header to complete (writing the frame
  // count at position 0 would overwrite the magic)
  if (m_headerWritten) {
    offsetMapPos = (m_exists ? m_offsetTablePos : ftell(m_chan));
    fseek(m_chan, offsetMapPos, SEEK_SET);

    TzlOffsetMap::iterator it = m_frameOffsTable.begin();
    for (; it != m_frameOffsTable.end(); ++it) {
      TFrameId fid      = it->first;
      TINT32 num        = fid.getNumber();
      QByteArray suffix = fid.getLetter().toUtf8();
      TINT32 offs       = it->second.m_offs;
      TINT32 length     = it->second.m_length;
      tfwrite(&num, 1, m_chan);
      if (m_version >= 15) {  // write the suffix length before data
        TINT32 suffixLength = suffix.size();
        tfwrite(&suffixLength, 1, m_chan);
        tfwrite(suffix.constData(), suffixLength, m_chan);
      } else  // write only the first byte
        tfwrite(suffix.constData(), 1, m_chan);
      tfwrite(&offs, 1, m_chan);
      tfwrite(&length, 1, m_chan);
    }

    // Write Icon Offset Table after frameOffsTable
    iconOffsetMapPos =
        ftell(m_chan);  //(m_exists?m_iconOffsetTablePos: ftell(m_chan));
    fseek(m_chan, iconOffsetMapPos, SEEK_SET);

    TzlOffsetMap::iterator iconIt = m_iconOffsTable.begin();
    for (; iconIt != m_iconOffsTable.end(); ++iconIt) {
      TFrameId fid           = iconIt->first;
      TINT32 num             = fid.getNumber();
      QByteArray suffix      = fid.getLetter().toUtf8();
      TINT32 thumbnailOffs   = iconIt->second.m_offs;
      TINT32 thumbnailLength = iconIt->second.m_length;
      tfwrite(&num, 1, m_chan);
      if (m_version >= 15) {  // write the suffix length before data
        TINT32 suffixLength = suffix.size();
        tfwrite(&suffixLength, 1, m_chan);
        tfwrite(suffix.constData(), suffixLength, m_chan);
      } else  // write only the first byte
        tfwrite(suffix.constData(), 1, m_chan);
      tfwrite(&thumbnailOffs, 1, m_chan);
      tfwrite(&thumbnailLength, 1, m_chan);
    }

    fseek(m_chan, m_frameCountPos, SEEK_SET);
    // the table, not the counter: remove() doesn't decrement it
    TINT32 frameCount = m_frameOffsTable.size();

    tfwrite(&frameCount, 1, m_chan);
    tfwrite(&offsetMapPos, 1, m_chan);
    tfwrite(&iconOffsetMapPos, 1, m_chan);
  }
  fclose(m_chan);
  m_chan = 0;

  if (m_palette && m_overwritePaletteFlag &&
      (m_palette->getDirtyFlag() ||
       !TSystem::doesExistFileOrLevel(m_palettePath))) {
    try {
      TOStream os(m_palettePath);
      os << m_palette;
    } catch (...) {
    }
    m_palette->release();
  }

  if (m_contentHistory) {
    TFilePath historyFp = m_path.withNoFrame().withType("hst");
    FILE *historyChan   = fopen(historyFp, "w");
    if (historyChan) {
      std::string historyData = m_contentHistory->serialize().toStdString();
      fwrite(&historyData[0], 1, historyData.length(), historyChan);
      fclose(historyChan);
    }
  }
  // Se lo spazio libero (cioè la somma di tutti i buchi che si sono creati tra
  // i frame)
  // è maggiore di una certa soglia oppure è stato rimosso almeno un frame
  // allora ottimizzo il file
  // (in pratica risalvo il file da capo senza buchi).
  if (getFreeSpace() > 0.3 || erasedFrame) {
    try {
      optimize();
    } catch (...) {
      // the file stays as saved, just not compacted
    }
  }
}

//-------------------------------------------------------------------

TImageWriterP TLevelWriterTzl::getFrameWriter(TFrameId fid) {
  return new TImageWriterTzl(this, fid);
}

//-------------------------------------------------------------------

void TLevelWriterTzl::writeHeader(const TDimension &size) {
  m_headerWritten   = true;
  const char *codec = "LZO ";
  int codecLen      = strlen(codec);
  TINT32 hdrSize    = 3 * sizeof(TINT32) + codecLen;
  TINT32 lx = size.lx, ly = size.ly, intval = 1;

  tfwrite(&hdrSize, 1, m_chan);
  tfwrite(&lx, 1, m_chan);
  tfwrite(&ly, 1, m_chan);
  m_frameCountPos = ftell(m_chan);

  assert(m_frameCountPos == 8 + CREATOR_LENGTH + 3 * sizeof(TINT32));

  // I put the place for the frameCount, which I will write in this position at
  // the end  (see in the destructor)
  tfwrite(&intval, 1, m_chan);
  // I put the place for the offsetTableOffset, which I will write in this
  // position at the end  (see in the destructor)
  intval = 0;
  tfwrite(&intval, 1, m_chan);
  // I put the place for the iconOffsetTableOffset, which I will write in this
  // position at the end  (see in the destructor)
  tfwrite(&intval, 1, m_chan);
  tfwrite(codec, codecLen, m_chan);
}

//-------------------------------------------------------------------

void TLevelWriterTzl::addFreeChunk(TINT32 offs, TINT32 length) {
  std::set<TzlChunk>::iterator it = m_freeChunks.begin();
  while (it != m_freeChunks.end()) {
    // if (it->m_offs>offs+length+1)
    //  break;

    if (it->m_offs + it->m_length == offs)  // accorpo due chunks in uno
    {
      TzlChunk chunk(it->m_offs, it->m_length + length);
      m_freeChunks.erase(it);
      m_freeChunks.insert(chunk);
      // it->m_length += length;
      return;
    } else if (offs + length == it->m_offs) {
      TzlChunk chunk(offs, it->m_length + length);
      m_freeChunks.erase(it);
      m_freeChunks.insert(chunk);
      // it->m_offs = offs;
      // it->m_length += length;
      return;
    }
    it++;
  }
  if (it == m_freeChunks.end()) m_freeChunks.insert(TzlChunk(offs, length));
}
//-------------------------------------------------------------------

TINT32 TLevelWriterTzl::findSavingChunk(const TFrameId &fid, TINT32 length,
                                        bool isIcon) {
  TzlOffsetMap::iterator it;
  // prima libero il chunk del fid, se c'e'. accorpo con altro chunk se trovo
  // uno contiguo.
  if (!isIcon) {
    if ((it = m_frameOffsTable.find(fid)) != m_frameOffsTable.end()) {
      addFreeChunk(it->second.m_offs, it->second.m_length);
      m_frameOffsTable.erase(it);
    } else
      m_frameCount++;
  } else {
    if ((it = m_iconOffsTable.find(fid)) != m_iconOffsTable.end()) {
      addFreeChunk(it->second.m_offs, it->second.m_length);
      m_iconOffsTable.erase(it);
    }
  }

  // ora cerco un cioncone libero con la piu' piccola memoria sufficiente
  std::set<TzlChunk>::iterator it1   = m_freeChunks.begin(),
                               found = m_freeChunks.end();
  for (; it1 != m_freeChunks.end(); it1++) {
    //   TINT32 _length = it1->m_length;

    if (it1->m_length >= length &&
        (found == m_freeChunks.end() || found->m_length > it1->m_length))
      found = it1;
  }

  if (found != m_freeChunks.end()) {
    //  TINT32 _length = found->m_length;
    TINT32 _offset = found->m_offs;
    if (found->m_length > length) {
      TzlChunk chunk(found->m_offs + length, found->m_length - length);
      m_freeChunks.insert(chunk);
      // found->m_offs+=length;
      // found->m_length-=length;
    } else
      assert(found->m_length == length);
    m_freeChunks.erase(found);
    return _offset;
  } else {
    m_offsetTablePos += length;
    return m_offsetTablePos - length;
  }
}
//-------------------------------------------------------------------
bool TLevelWriterTzl::convertToLatestVersion() {
  TFileStatus fs(m_path);
  // se il file è di una versione precedente deve necessariamente già esistere
  // su disco
  assert(fs.doesExist());
  if (!fs.doesExist()) return false;
  m_exists         = false;
  m_frameCount     = 0;
  m_frameOffsTable = TzlOffsetMap();
  m_iconOffsTable  = TzlOffsetMap();
  m_headerWritten  = false;

  fclose(m_chan);
  m_chan = 0;

  std::string tempName = "~" + m_path.getName() + "tmp&.tlv";
  TFilePath tempPath   = TSystem::getTempDir() + tempName;
  if (TSystem::doesExistFileOrLevel(m_path)) {
    assert(m_path != tempPath);
    if (TSystem::doesExistFileOrLevel(tempPath)) TSystem::deleteFile(tempPath);
    TSystem::copyFile(tempPath, m_path);
  } else
    return false;
  m_chan = fopen(m_path, "rb+");
  if (!m_chan) return false;
  if (!writeVersionAndCreator(m_chan, m_magic, m_creator)) return false;
  m_creatorWritten = true;
  m_version        = currentVersion();
  // Every frame must be converted: on any failure put the original back
  bool converted = false;
  try {
    TLevelReaderP lr(tempPath);
    TLevelP level = lr ? lr->loadInfo() : TLevelP();
    if (level) {
      converted = true;
      for (TLevel::Iterator it = level->begin(); it != level->end(); ++it) {
        TImageP img = lr->getFrameReader(it->first)->load();
        if (!img) {
          converted = false;
          break;
        }
        save(img, it->first);
      }
    }
  } catch (...) {
    converted = false;
  }
  if (!converted) {
    if (m_chan) fclose(m_chan);
    m_chan = 0;
    // the temp copy is the only original left: delete it only once restored
    if (replaceWithCopy(m_path, tempPath)) TSystem::deleteFile(tempPath);
    return false;
  }
  if (TSystem::doesExistFileOrLevel(tempPath)) {
    TSystem::deleteFile(tempPath);
  }

  TINT32 offsetMapPos     = 0;
  TINT32 iconOffsetMapPos = 0;
  if (!m_chan) return false;

  offsetMapPos = (m_exists ? m_offsetTablePos : ftell(m_chan));
  fseek(m_chan, offsetMapPos, SEEK_SET);

  TzlOffsetMap::iterator it2 = m_frameOffsTable.begin();
  for (; it2 != m_frameOffsTable.end(); ++it2) {
    TFrameId fid      = it2->first;
    TINT32 num        = fid.getNumber();
    QByteArray suffix = fid.getLetter().toUtf8();
    TINT32 offs       = it2->second.m_offs;
    TINT32 length     = it2->second.m_length;
    tfwrite(&num, 1, m_chan);
    if (m_version >= 15) {  // write the suffix length before data
      TINT32 suffixLength = suffix.size();
      tfwrite(&suffixLength, 1, m_chan);
      tfwrite(suffix.constData(), suffixLength, m_chan);
    } else  // write only the first byte
      tfwrite(suffix.constData(), 1, m_chan);
    tfwrite(&offs, 1, m_chan);
    tfwrite(&length, 1, m_chan);
  }

  iconOffsetMapPos = ftell(m_chan);
  fseek(m_chan, iconOffsetMapPos, SEEK_SET);

  TzlOffsetMap::iterator iconIt = m_iconOffsTable.begin();
  for (; iconIt != m_iconOffsTable.end(); ++iconIt) {
    TFrameId fid           = iconIt->first;
    TINT32 num             = fid.getNumber();
    QByteArray suffix      = fid.getLetter().toUtf8();
    TINT32 thumbnailOffs   = iconIt->second.m_offs;
    TINT32 thumbnailLength = iconIt->second.m_length;
    tfwrite(&num, 1, m_chan);
    if (m_version >= 15) {  // write the suffix length before data
      TINT32 suffixLength = suffix.size();
      tfwrite(&suffixLength, 1, m_chan);
      tfwrite(suffix.constData(), suffixLength, m_chan);
    } else  // write only the first byte
      tfwrite(suffix.constData(), 1, m_chan);
    tfwrite(&thumbnailOffs, 1, m_chan);
    tfwrite(&thumbnailLength, 1, m_chan);
  }

  fseek(m_chan, m_frameCountPos, SEEK_SET);
  TINT32 frameCount = m_frameOffsTable.size();
  tfwrite(&frameCount, 1, m_chan);
  tfwrite(&offsetMapPos, 1, m_chan);
  tfwrite(&iconOffsetMapPos, 1, m_chan);
  m_frameOffsTable = TzlOffsetMap();
  m_iconOffsTable  = TzlOffsetMap();
  m_frameCount     = 0;
  m_res            = TDimension();
  fclose(m_chan);
  m_chan = 0;

  if (m_palette && (m_palette->getDirtyFlag() ||
                    !TSystem::doesExistFileOrLevel(m_palettePath))) {
    TOStream os(m_palettePath);
    os << m_palette;
  }
  // else
  //	m_palette = 0;

  m_chan = fopen(m_path, "rb+");
  if (!m_chan) return false;
  if (!readHeaderAndOffsets(m_chan, m_frameOffsTable, m_iconOffsTable, m_res,
                            m_version, m_creator, &m_frameCount,
                            &m_offsetTablePos, &m_iconOffsetTablePos, 0))
    return false;
  m_freeChunks.clear();
  buildFreeChunksTable();
  m_headerWritten = true;
  m_exists        = true;
  m_frameCountPos = 8 + CREATOR_LENGTH + 3 * sizeof(TINT32);
  assert(m_version == currentVersion());
  if (!m_renumberTable.empty()) renumberFids(m_renumberTable);
  return true;
}
//-------------------------------------------------------------------
void TLevelWriterTzl::saveImage(const TImageP &img, const TFrameId &_fid,
                                bool isIcon) {
  TFrameId fid = _fid;
  if (!m_chan) return;

  // se il file è di una versione precedente allora lo converto prima
  if (m_version < currentVersion()) {
    if (!convertToLatestVersion()) return;
    assert(m_version == currentVersion());
  }

  if (!m_updatedIconsSize && m_exists)
    m_updatedIconsSize = checkIconSize(getIconSize());

  TToonzImageP ti = img;
  if (!ti) return;

  if (!m_palette) {
    m_palette = ti->getPalette();
    if (m_palette) m_palette->addRef();
  }

  if (!m_creatorWritten) {
    fseek(m_chan, 0, SEEK_SET);
    writeVersionAndCreator(m_chan, m_magic, m_creator);
    m_creatorWritten = true;
  }
  if (!m_headerWritten) writeHeader(ti->getSize());

  if (m_res != TDimension())
    if (!isIcon)
      assert(m_res == ti->getSize());
    else
      assert(getIconSize() == ti->getSize());
  else
    m_res = ti->getSize();

  TRect sb = ti->getSavebox();

  TINT32 sbx0 = sb.x0, sby0 = sb.y0, sblx = sb.getLx(), sbly = sb.getLy();
  TINT32 buffSize = 0;

  UCHAR *buff = 0;  //  = ti->getCompressedBuffer(buffSize);

  TRasterP cmras = ti->getRaster();

  assert(cmras);

  if (cmras->getBounds() != sb) {
    TRect rectToExtract = sb;
    if (!rectToExtract.isEmpty()) {
      TRasterP smallRaster = cmras->extract(rectToExtract)->clone();
      assert(rectToExtract == sb);
      cmras = smallRaster;
    } else {
      cmras = cmras->create(1, 1);
      cmras->clear();
    }
  }

  TRasterGR8P rCompressed = m_codec->compress(cmras, 1, buffSize);
  if (rCompressed == TRasterGR8P()) {
    static bool firstTime = true;
    if (firstTime) {
      firstTime = false;
      // MessageBox( NULL, "Cannot save level, out of memory!", "Warning!",
      // MB_OK);
    }
    return;
  }

  rCompressed->lock();

  buffSize = rCompressed->getLx();
  buff     = rCompressed->getRawData();

  assert(buffSize);
  assert(buff);

#if !TNZ_LITTLE_ENDIAN

  Header *header = (Header *)buff;

  TRasterP ras;
  m_codec->decompress(buff, buffSize, ras, false);
  delete[] buff;
  assert((TRasterCM32P)ras);
  assert(ras->getLx() == header->m_lx);
  assert(ras->getLy() == header->m_ly);

  for (int y = 0; y < ras->getLy(); ++y) {
    ras->lock();
    TINT32 *pix    = ((TINT32 *)ras->getRawData()) + y * ras->getLx();
    TINT32 *endPix = pix + ras->getLx();
    while (pix < endPix) {
      *pix = swapTINT32(*pix);
      pix++;
    }
    ras->unlock();
  }

  rCompressed = m_codec->compress(ras, 1, buffSize);

  header = (Header *)buff;

  header->m_lx      = swapTINT32(header->m_lx);
  header->m_ly      = swapTINT32(header->m_ly);
  header->m_rasType = (Header::RasType)swapTINT32(header->m_rasType);
#endif

  double xdpi = 1, ydpi = 1;
  ti->getDpi(xdpi, ydpi);

  TINT32 theBuffSize = buffSize;

  TINT32 length = 0;
  if (!isIcon)
    length = 5 * sizeof(TINT32) + 2 * sizeof(double) + theBuffSize;
  else
    length = 3 * sizeof(TINT32) + theBuffSize;

  if (fid == TFrameId()) {
    assert(!m_exists);
    if (m_frameOffsTable.empty())
      fid = (m_frameOffsTable.empty())
                ? TFrameId(1, 0)
                : TFrameId(m_frameOffsTable.rbegin()->first.getNumber() + 1, 0);
    if (m_iconOffsTable.empty())
      fid = (m_iconOffsTable.empty())
                ? TFrameId(1, 0)
                : TFrameId(m_iconOffsTable.rbegin()->first.getNumber() + 1, 0);
  }

  if (!m_exists) {
    long offs = ftell(m_chan);
    if (!isIcon) {
      m_frameOffsTable[fid] = TzlChunk(offs, length);
      m_frameCount++;
    } else
      m_iconOffsTable[fid] = TzlChunk(offs, length);

  } else {
    long frameOffset = findSavingChunk(fid, length, isIcon);
    if (!isIcon)
      m_frameOffsTable[fid] = TzlChunk(frameOffset, length);
    else
      m_iconOffsTable[fid] = TzlChunk(frameOffset, length);
    fseek(m_chan, frameOffset, SEEK_SET);
  }
  if (!isIcon) {
    tfwrite(&sbx0, 1, m_chan);
    tfwrite(&sby0, 1, m_chan);
    tfwrite(&sblx, 1, m_chan);
    tfwrite(&sbly, 1, m_chan);
    tfwrite(&buffSize, 1, m_chan);
    tfwrite(&xdpi, 1, m_chan);
    tfwrite(&ydpi, 1, m_chan);
    tfwrite(buff, theBuffSize, m_chan);
  } else {
    TINT32 iconLx = getIconSize().lx;
    TINT32 iconLy = getIconSize().ly;
    tfwrite(&iconLx, 1, m_chan);
    tfwrite(&iconLy, 1, m_chan);
    tfwrite(&buffSize, 1, m_chan);
    tfwrite(buff, theBuffSize, m_chan);
  }
  rCompressed->unlock();

  // #if !TNZ_LITTLE_ENDIAN
  //  delete [] buff;
  // #endif
}

//-------------------------------------------------------------------

void TLevelWriterTzl::doSave(const TImageP &img, const TFrameId &_fid) {
  // salvo l'immagine grande
  saveImage(img, _fid);
  if (!img)
    throw(TException(
        "Saving tlv: it is not possible to create the image frame."));
  // creo l'iconcina
  TImageP icon = TImageP();
  createIcon(img, icon);
  assert(icon);
  if (!icon)
    throw(
        TException("Saving tlv: it is not possible to create the image icon."));
  // salvo l'iconcina
  saveImage(icon, _fid, true);
  return;
}

//-------------------------------------------------------------------
void TLevelWriterTzl::save(const TImageP &img, const TFrameId &fid) {
  doSave(img, fid);
}
void TLevelWriterTzl::save(const TImageP &img) { doSave(img, TFrameId()); }
//-------------------------------------------------------------------
void TLevelWriterTzl::saveIcon(const TImageP &img, const TFrameId &fid) {
  // salvo su file come icona
  saveImage(img, fid, true);
}
//-------------------------------------------------------------------

void TLevelWriterTzl::setPalette(TPalette *palette) {
  if (!m_palette) {
    m_palette = palette;
    m_palette->addRef();
  }
}
//-------------------------------------------------------------------
void TLevelWriterTzl::renumberFids(
    const std::map<TFrameId, TFrameId> &renumberTable) {
  m_renumberTable = renumberTable;
  if (m_frameOffsTable.empty() || !m_exists) return;

  // Rimozione dei frame non presenti nella prima colonna di renumberTable
  std::map<TFrameId, TzlChunk> frameOffsTableTemp;
  frameOffsTableTemp        = m_frameOffsTable;
  TzlOffsetMap::iterator it = frameOffsTableTemp.begin();
  while (it != frameOffsTableTemp.end()) {
    TFrameId fid                                     = it->first;
    std::map<TFrameId, TFrameId>::const_iterator it2 = renumberTable.find(fid);
    if (it2 == renumberTable.end()) remove(fid);
    it++;
  }

  std::map<TFrameId, TzlChunk> frameOffsTable, iconOffsTable;
  // Renumber Frame
  std::map<TFrameId, TFrameId>::const_iterator it3 = renumberTable.begin();
  while (it3 != renumberTable.end()) {
    TFrameId fidToRenumber        = it3->first;
    TzlOffsetMap::iterator it     = m_frameOffsTable.find(fidToRenumber);
    TzlOffsetMap::iterator iconIt = m_iconOffsTable.find(fidToRenumber);
    if (it != m_frameOffsTable.end()) frameOffsTable[it3->second] = it->second;
    if (m_iconOffsTable.size() > 0 && iconIt != m_iconOffsTable.end())
      iconOffsTable[it3->second] = iconIt->second;
    it3++;
  }

  m_frameOffsTable.clear();
  m_frameOffsTable = frameOffsTable;
  m_iconOffsTable.clear();
  m_iconOffsTable = iconOffsTable;
  m_frameCount    = m_frameOffsTable.size();
  // buildFreeChunksTable();
}

//-------------------------------------------------------------------

void TLevelWriterTzl::createIcon(const TImageP &imgIn, TImageP &imgOut) {
  // Creo iconcina e poi la salvo
  TToonzImageP ti = imgIn;
  if (!ti) return;
  TRect sb    = ti->getSavebox();
  TINT32 sbx0 = sb.x0, sby0 = sb.y0, sblx = sb.getLx(), sbly = sb.getLy();
  TRasterP cmras = ti->getRaster();

  assert(m_iconSize.lx > 0 && m_iconSize.ly > 0);
  if (m_iconSize.lx <= 0 || m_iconSize.ly <= 0) return;
  TINT32 iconLx = m_iconSize.lx, iconLy = m_iconSize.ly;
  if (cmras->getLx() <= iconLx && cmras->getLy() <= iconLy) {
    iconLx     = cmras->getLx();
    iconLy     = cmras->getLy();
    m_iconSize = TDimension(iconLx, iconLy);
    imgOut     = imgIn;
    return;
  }

  TRasterCM32P thumbnailRas(iconLx, iconLy);
  if (iconLx > 0 && iconLy > 0) {
    // Do thumbnail
    TRop::makeIcon(thumbnailRas, TRasterCM32P(cmras));
    assert(thumbnailRas);
    if (!thumbnailRas) return;
  }
  // Compute savebox
  if (ti->getSize().lx <= 0 || ti->getSize().ly <= 0) return;
  TRect tmp_savebox;
  TRect savebox;
  TRop::computeBBox(thumbnailRas, tmp_savebox);
  if (tmp_savebox.isEmpty()) {
    TINT32 iconsbx0 = tround((double)iconLx * sbx0 / ti->getSize().lx);
    TINT32 iconsby0 = tround((double)iconLy * sby0 / ti->getSize().ly);
    TINT32 iconsblx =
        std::max(tround((double)iconLx * sblx / ti->getSize().lx), 1);
    TINT32 iconsbly =
        std::max(tround((double)iconLy * sbly / ti->getSize().ly), 1);
    savebox = TRect(TPoint(iconsbx0, iconsby0), TDimension(iconsblx, iconsbly));
  } else {
    savebox = tmp_savebox;
  }

  if (!TRect(m_iconSize).contains(savebox))  // it should be better to use
                                             // tfloor instead of tround in the
                                             // previous lines:
    // sometimes, for rounding problems, the savebox is outside the 1 pixel
    // raster size.
    // the best solution should be to replace tround with tfloor here
    // and in the icon reading of tzl 1.4 (method load14),
    // but this way the old 1.4 tzl are not read correctly (crash!)
    // so, this 'if' is a patch. vinz
    savebox = savebox * TRect(m_iconSize);
  thumbnailRas->clearOutside(savebox);
  TToonzImageP ticon(thumbnailRas, savebox);
  double xdpi = 1, ydpi = 1;
  ti->getDpi(xdpi, ydpi);
  ticon->setDpi(xdpi, ydpi);
  ticon->setPalette(ti->getPalette());
  imgOut = TImageP(ticon);
}

//-------------------------------------------------------------------

void TLevelWriterTzl::remove(const TFrameId &fid) {
  TzlOffsetMap::iterator it = m_frameOffsTable.find(fid);
  // se il fid non esiste non faccio nulla
  if (it == m_frameOffsTable.end()) return;
  // aggiungo spazio vuoto
  // m_freeChunks.insert(TzlChunk(it->second.m_offs, it->second.m_length));
  addFreeChunk(it->second.m_offs, it->second.m_length);
  // cancello l'immagine dalla tabella
  m_frameOffsTable.erase(it);

  if (m_iconOffsTable.size() > 0) {
    TzlOffsetMap::iterator iconIt = m_iconOffsTable.find(fid);
    // deve necessariamente esserci anche l'iconcina
    assert(iconIt != m_iconOffsTable.end());
    if (iconIt == m_iconOffsTable.end()) return;
    // aggiungo spazio vuoto
    // m_freeChunks.insert(TzlChunk(iconIt->second.m_offs,
    // iconIt->second.m_length));
    addFreeChunk(iconIt->second.m_offs, iconIt->second.m_length);
    // Cancello la relativa icona
    m_iconOffsTable.erase(iconIt);
    erasedFrame = true;
  }
}

//-------------------------------------------------------------------

void TLevelWriterTzl::setIconSize(TDimension iconSize) {
  assert(iconSize.lx > 0 && iconSize.ly > 0);

  m_iconSize     = TDimension(iconSize.lx, iconSize.ly);
  m_userIconSize = TDimension(
      iconSize.lx, iconSize.ly);  // Used in TLevelWriterTzl::optimize().
                                  // Observe that m_iconSize may change below.
  if (m_exists) {
    // A supported level file already exists at the specified path

    // Check whether icons stored in the file already
    // have the required size
    if (!m_updatedIconsSize)  // m_updatedIconsSize tells whether
      m_updatedIconsSize =
          checkIconSize(m_iconSize);  // icon sizes in the file match m_iconSize

    if (!m_updatedIconsSize) {
      // Icons in the stored file mismatch with the
      // required size. They need to be resized.
      m_updatedIconsSize = resizeIcons(m_iconSize);
      assert(m_updatedIconsSize);
    }
  }
}

//-------------------------------------------------------------------

/*!
  \brief    Checks the icons size stored inside an already existing level
            file, and compares it with the specified newSize.
  \return   Whether the specified newSize \a matches the icons size in
            the level file.
*/

bool TLevelWriterTzl::checkIconSize(const TDimension &newSize) {
  if (!m_exists || m_iconOffsTable.empty() || !m_chan) return false;

  // Read the size of icons in the file
  TINT32 iconLx = 0, iconLy = 0;

  long currentPos =
      ftell(m_chan);  // Backup current reading position in the file

  TzlOffsetMap::iterator it = m_iconOffsTable.begin();
  long offs                 = it->second.m_offs;

  fseek(m_chan, offs, SEEK_SET);

  bool ok = readValue(m_chan, iconLx) && readValue(m_chan, iconLy);

  fseek(m_chan, currentPos,
        SEEK_SET);  // Reset to the original position in the file

  if (!ok || iconLx <= 0 || iconLy <= 0 || iconLx > m_res.lx ||
      iconLy > m_res.ly)
    return false;

  m_currentIconSize = TDimension(
      iconLx, iconLy);  // This is not performed if the above preemptive
                        // bailout kicks in. BTW, m_currentIconSize
                        // is currently UNUSED! Should be cleaned up...
  // Compare newSize against read icon size in the file
  return (m_currentIconSize == newSize);
}

//-------------------------------------------------------------------

bool TLevelWriterTzl::resizeIcons(const TDimension &newSize) {
  if (!m_exists) return false;
  if (m_iconOffsTable.empty()) return false;
  if (!m_chan) return false;

  // make a copy of m_path to be able to use it for resizeIcons()
  fclose(m_chan);
  m_chan = 0;
  TFileStatus fs(m_path);
  assert(fs.doesExist());
  if (!fs.doesExist()) return false;
  std::string tempName = "~" + m_path.getName() + "tmpIcon&.tlv";
  TFilePath tempPath   = TSystem::getTempDir() + tempName;
  if (TSystem::doesExistFileOrLevel(m_path)) {
    assert(m_path != tempPath);
    if (TSystem::doesExistFileOrLevel(tempPath)) TSystem::deleteFile(tempPath);
    TSystem::copyFile(tempPath, m_path);
  } else
    return false;
  m_chan = fopen(m_path, "rb+");
  if (!m_chan) return false;

  if (!TSystem::doesExistFileOrLevel(tempPath)) return false;
  // Remake every icon from its frame; a frame that can't be read keeps its
  // old icon
  {
    TLevelReaderP lr(tempPath);
    TLevelP level = lr ? lr->loadInfo() : TLevelP();
    if (level)
      for (TLevel::Iterator it = level->begin(); it != level->end(); ++it) {
        try {
          TImageP img = lr->getFrameReader(it->first)->load();
          if (!img) continue;
          TImageP icon;
          createIcon(img, icon);
          if (icon) saveIcon(icon, it->first);
        } catch (...) {
        }
      }
  }
  // delete m_tempPath
  if (TSystem::doesExistFileOrLevel(tempPath)) {
    TSystem::deleteFile(tempPath);
  }

  return true;
}

float TLevelWriterTzl::getFreeSpace() {
  if (!m_exists) return 0;
  TINT32 freeSpace = 0;
  for (const TzlChunk &chunk : m_freeChunks) freeSpace += chunk.m_length;
  TINT32 totalSpace = m_offsetTablePos - HEADER_END;
  return totalSpace > 0 ? (float)freeSpace / totalSpace : 0;
}
// creo il file ottimizzato, cioè senza spazi liberi
bool TLevelWriterTzl::optimize() {
  TFileStatus fs(m_path);
  assert(fs.doesExist());

  std::string tempName = "~" + m_path.getName() + "tmp&.tlv";
  TFilePath tempPath =
      TSystem::getTempDir() + tempName;  // Path temporaneo del file ottimizzato

  if (TSystem::doesExistFileOrLevel(tempPath)) TSystem::deleteFile(tempPath);

  // The compacted copy replaces the file only if every frame made it
  bool complete = true;
  {
    TLevelWriterP lw(tempPath);
    TLevelReaderP lr(m_path);
    lw->setIconSize(m_userIconSize);
    TLevelP level = lr ? lr->loadInfo() : TLevelP();
    if (!level || level->getFrameCount() == 0) complete = false;
    for (TLevel::Iterator it = level->begin(); complete && it != level->end();
         ++it) {
      TToonzImageP img;
      try {
        img = lr->getFrameReader(it->first)->load();
      } catch (...) {
      }
      if (!img) {
        complete = false;
        break;
      }
      lw->getFrameWriter(it->first)->save(img);
    }
  }
  if (!complete) {
    TSystem::deleteFile(tempPath);
    return false;
  }

  bool replaced = replaceWithCopy(m_path, tempPath);
  if (TSystem::doesExistFileOrLevel(tempPath)) TSystem::deleteFile(tempPath);
  return replaced;
}

//===================================================================
//
// TLevelReaderTzl
//
//-------------------------------------------------------------------

void TLevelReaderTzl::readPalette() {
  TFilePath pltfp = m_path.withNoFrame().withType("tpl");
  TPersist *p     = 0;
  TIStream is(pltfp);
  TPalette *palette = 0;

  if (is) {
    std::string tagName;
    if (is.matchTag(tagName) && tagName == "palette") {
      std::string gname;
      is.getTagParam("name", gname);
      palette = new TPalette();
      palette->loadData(is);
      palette->setGlobalName(::to_wstring(gname));
      is.matchEndTag();
    }
  }

  if (!palette) {
    int i;
    palette = new TPalette();
    for (i = 1; i < 128 + 32; i++) palette->addStyle(TPixel32(127, 150, 255));

    for (i = 0; i < 10; i++) palette->getPage(0)->addStyle(i + 1);
    for (i = 0; i < 10; i++) palette->getPage(0)->addStyle(128 + i);
  }
  if (m_level) m_level->setPalette(palette);
}

//---------------------------------------------------

TLevelReaderTzl::TLevelReaderTzl(const TFilePath &path)
    : TLevelReader(path)
    , m_chan(0)
    , m_res(0, 0)
    , m_xDpi(0)
    , m_yDpi(0)
    , m_version(0)
    , m_frameOffsTable()
    , m_iconOffsTable()
    , m_level()
    , m_readPalette(true) {
  m_chan = fopen(path, "rb");

  if (!m_chan) return;

  if (!readHeaderAndOffsets(m_chan, m_frameOffsTable, m_iconOffsTable, m_res,
                            m_version, m_creator, 0, 0, 0, m_level)) {
    // nothing of a damaged file is shown
    m_frameOffsTable.clear();
    m_iconOffsTable.clear();
    m_res = TDimension(0, 0);
    m_level = TLevelP();
    fclose(m_chan);
    m_chan = 0;
    return;
  }

  TFilePath historyFp = path.withNoFrame().withType("hst");
  FILE *historyChan   = fopen(historyFp, "rb");
  if (historyChan) {
    // read the whole file into historyData string
    long lSize = -1;
    if (!fseek(historyChan, 0, SEEK_END)) lSize = ftell(historyChan);
    if (lSize > 0 && !fseek(historyChan, 0, SEEK_SET)) {
      std::string historyData(lSize, '\0');
      historyData.resize(fread(&historyData[0], 1, lSize, historyChan));
      if (!m_contentHistory) m_contentHistory = new TContentHistory(true);
      m_contentHistory->deserialize(QString::fromStdString(historyData));
    }
    fclose(historyChan);
  }
}

//-------------------------------------------------------------------

void TLevelReaderTzl::doReadPalette(bool doReadIt) { m_readPalette = doReadIt; }

//-------------------------------------------------------------------

TLevelReaderTzl::~TLevelReaderTzl() {
  if (m_chan) fclose(m_chan);
  m_chan = 0;
}

//-------------------------------------------------------------------

TLevelP TLevelReaderTzl::loadInfo() {
  if (m_level && m_level->getPalette() == 0 && m_readPalette) readPalette();
  return m_level;
}

//-------------------------------------------------------------------

TImageReaderP TLevelReaderTzl::getFrameReader(TFrameId fid) {
  if (m_level && m_level->getPalette() == 0 && m_readPalette) readPalette();

  return new TImageReaderTzl(getFilePath(), fid, this);
}

//-------------------------------------------------------------------
QString TLevelReaderTzl::getCreator() { return m_creator; }

//-------------------------------------------------------------------
bool TLevelReaderTzl::getIconSize(TDimension &iconSize) {
  if (m_iconOffsTable.empty() || !m_chan) return false;
  long currentPos = ftell(m_chan);
  TINT32 iconLx = 0, iconLy = 0;
  bool ok = !fseek(m_chan, m_iconOffsTable.begin()->second.m_offs, SEEK_SET) &&
            readValue(m_chan, iconLx) && readValue(m_chan, iconLy);
  fseek(m_chan, currentPos, SEEK_SET);
  if (!ok || iconLx <= 0 || iconLy <= 0 || iconLx > m_res.lx ||
      iconLy > m_res.ly)
    return false;
  iconSize = TDimension(iconLx, iconLy);
  return true;
}

//===================================================================
//
// TImageReaderTzl
//
//-------------------------------------------------------------------

TImageReaderTzl::TImageReaderTzl(const TFilePath &f, const TFrameId &fid,
                                 TLevelReaderTzl *lr)
    : TImageReader(f)
    , m_fid(fid)
    , m_lrp(lr)
    , m_lx(lr->m_res.lx)
    , m_ly(lr->m_res.ly)
    , m_isIcon(false) {}

//-------------------------------------------------------------------

namespace {

//! The fields written before a frame's compressed data
struct FrameFields {
  TINT32 sbx0, sby0, sblx, sbly, buffSize;
  double xdpi, ydpi;
};

//! Reads the fields of a frame and checks them against the level size and
//! the frame's chunk. Leaves the file at the start of the compressed data.
bool readFrameFields(FILE *chan, const TzlChunk &chunk, const TDimension &res,
                     FrameFields &f) {
  if (fseek(chan, chunk.m_offs, SEEK_SET) || !readValue(chan, f.sbx0) ||
      !readValue(chan, f.sby0) || !readValue(chan, f.sblx) ||
      !readValue(chan, f.sbly) || !readValue(chan, f.buffSize) ||
      !readValue(chan, f.xdpi) || !readValue(chan, f.ydpi))
    return false;
  return f.sbx0 >= 0 && f.sby0 >= 0 && f.sblx >= 0 && f.sbly >= 0 &&
         (long long)f.sbx0 + f.sblx <= res.lx &&
         (long long)f.sby0 + f.sbly <= res.ly &&
         f.buffSize > (TINT32)sizeof(Header) &&
         f.buffSize <= chunk.m_length - FRAME_FIELDS_SIZE;
}

//! Reads buffSize bytes of compressed data and decompresses them. Returns 0
//! unless they hold a colormap raster whose size is accepted by sizeOk.
template <class SizeOk>
TRasterCM32P readCompressedRaster(FILE *chan, TINT32 buffSize, SizeOk sizeOk) {
  std::vector<UCHAR> buff(buffSize);
  if (fread(buff.data(), 1, buffSize, chan) != (size_t)buffSize)
    return TRasterCM32P();
  Header header;
  memcpy(&header, buff.data(), sizeof header);
  if (header.m_rasType != Header::Raster32CM || header.m_lx <= 0 ||
      header.m_ly <= 0 || !sizeOk(TDimension(header.m_lx, header.m_ly)))
    return TRasterCM32P();
  TRasterCodecLZO codec("LZO", false);
  TRasterP ras;
  if (!codec.decompress(buff.data(), buffSize, ras, false))
    return TRasterCM32P();
  TRasterCM32P cmras = ras;
  if (!cmras || cmras->getLx() != header.m_lx ||
      cmras->getLy() != header.m_ly)
    return TRasterCM32P();
  return cmras;
}

}  // namespace

//-------------------------------------------------------------------

TImageP TImageReaderTzl::load14() {
  FILE *chan = m_lrp->m_chan;
  if (!chan) return TImageP();

  TzlOffsetMap::iterator it = m_lrp->m_frameOffsTable.find(m_fid);
  if (it == m_lrp->m_frameOffsTable.end())
    throw TException("Loading tlv: frame ID not found.");

  FrameFields f;
  if (!readFrameFields(chan, it->second, m_lrp->m_res, f))
    throw TException("Loading tlv: damaged frame header.");
  TRect savebox(TPoint(f.sbx0, f.sby0), TDimension(f.sblx, f.sbly));
  TDimension imgSize(m_lx, m_ly);

  if (m_isIcon) {
    TzlOffsetMap::iterator iconIt = m_lrp->m_iconOffsTable.find(m_fid);
    if (iconIt == m_lrp->m_iconOffsTable.end())
      throw TException("Loading tlv: icon not found.");
    TINT32 iconLx, iconLy, buffSize;
    if (fseek(chan, iconIt->second.m_offs, SEEK_SET) ||
        !readValue(chan, iconLx) || !readValue(chan, iconLy) ||
        !readValue(chan, buffSize) || iconLx <= 0 || iconLy <= 0 ||
        iconLx > m_lx || iconLy > m_ly ||
        buffSize <= (TINT32)sizeof(Header) ||
        buffSize > iconIt->second.m_length - ICON_FIELDS_SIZE)
      throw TException("Loading tlv: damaged icon header.");

    TRasterCM32P ras =
        readCompressedRaster(chan, buffSize, [&](const TDimension &d) {
          return d.lx <= iconLx && d.ly <= iconLy;
        });
    if (!ras) throw TException("Loading tlv: damaged icon.");

    TRect tmp_savebox;
    TRect iconSavebox;
    TRop::computeBBox(ras, tmp_savebox);
    if (tmp_savebox.isEmpty()) {
      TINT32 iconsbx0 = tround((double)iconLx * f.sbx0 / m_lx);
      TINT32 iconsby0 = tround((double)iconLy * f.sby0 / m_ly);
      TINT32 iconsblx = std::max(tround((double)iconLx * f.sblx / m_lx), 1);
      TINT32 iconsbly = std::max(tround((double)iconLy * f.sbly / m_ly), 1);
      iconSavebox =
          TRect(TPoint(iconsbx0, iconsby0), TDimension(iconsblx, iconsbly));
    } else {
      TINT32 iconsbx0 = tfloor((double)iconLx * f.sbx0 / m_lx);
      TINT32 iconsby0 = tfloor((double)iconLy * f.sby0 / m_ly);
      iconSavebox     = TRect(TPoint(iconsbx0, iconsby0),
                              TDimension(tmp_savebox.getLx(), tmp_savebox.getLy()));
    }

    TDimension iconSize(iconLx, iconLy);
    // see the comment in createIcon about rounding
    if (!TRect(iconSize).contains(iconSavebox))
      iconSavebox = iconSavebox * TRect(iconSize);

    if (iconSize != iconSavebox.getSize()) {
      if (iconSavebox.getSize() != ras->getSize())
        throw TException("Loading tlv: bad icon savebox size.");
      TRasterCM32P fullRas(iconSize);
      fullRas->fillOutside(iconSavebox, TPixelCM32());
      fullRas->extractT(iconSavebox)->copy(ras);
      ras = fullRas;
    }

    TToonzImageP ti(ras, iconSavebox);
    ti->setDpi(f.xdpi, f.ydpi);
    ti->setPalette(m_lrp->m_level->getPalette());
    return ti;
  }

  // An empty savebox is saved as a 1x1 raster
  TRasterCM32P ras =
      readCompressedRaster(chan, f.buffSize, [&](const TDimension &d) {
        return savebox.isEmpty() ? d.lx <= m_lx && d.ly <= m_ly
                                 : d == savebox.getSize();
      });
  if (!ras) throw TException("Loading tlv: damaged frame.");

  if (imgSize != savebox.getSize()) {
    TRasterCM32P fullRas(imgSize);
    if (savebox.isEmpty())
      fullRas->clear();
    else {
      fullRas->fillOutside(savebox, TPixelCM32());
      fullRas->extractT(savebox)->copy(ras);
    }
    ras = fullRas;
  }

  TToonzImageP ti(ras, savebox);
  ti->setDpi(f.xdpi, f.ydpi);
  ti->setPalette(m_lrp->m_level->getPalette());
  return ti;
}

//-------------------------------------------------------------------

// Restituisce la regione del raster shrinkata e la relativa savebox.
static TRect applyShrinkAndRegion(TRasterP &ras, int shrink, TRect region,
                                  TRect savebox) {
  // estraggo la regione solo se essa ha coordinate valide.
  if (!region.isEmpty() && region != TRect() && region.getLx() > 0 &&
      region.getLy() > 0)
    ras = ras->extract(region);
  else
    region = TRect(0, 0, ras->getLx() - 1, ras->getLy() - 1);
  if (shrink > 1) {
    ras = TRop::shrink(ras, shrink);
  }
  // calculate the new savebox
  savebox *= region;
  if (savebox == TRect() || savebox.getLx() <= 0 || savebox.getLy() <= 0)
    return TRect();
  int firstColIndexOfLayer = (savebox.x0 - region.x0) - 1 + shrink -
                             (abs(savebox.x0 - region.x0 - 1) % shrink);
  int firstRowIndexOfLayer = (savebox.y0 - region.y0) - 1 + shrink -
                             (abs(savebox.y0 - region.y0 - 1) % shrink);
  savebox.x0 = (firstColIndexOfLayer) / shrink;
  savebox.y0 = (firstRowIndexOfLayer) / shrink;
  savebox.x1 = savebox.x0 + (savebox.getLx()) / shrink - 1;
  savebox.y1 = savebox.y0 + (savebox.getLy()) / shrink - 1;
  return savebox;
}

//-------------------------------------------------------------------

TImageP TImageReaderTzl::load() {
  if (m_lrp->m_frameOffsTable.empty()) return TImageP();
  TImageP image = load14();
  if (image == TImageP()) return TImageP();

  if (!m_isIcon) {
    TToonzImageP ti = image;
    if (!ti) return TImageP();
    TRasterP ras  = ti->getRaster();
    TRect savebox = ti->getSavebox();
    if (m_region != TRect() && m_region.getLx() > 0 && m_region.getLy() > 0) {
      m_region *= TRect(0, 0, ti->getSize().lx, ti->getSize().ly);
      if (m_region.isEmpty() || m_region == TRect() || m_region.getLx() <= 0 ||
          m_region.getLy() <= 0)
        return TImageP();
    }
    savebox = applyShrinkAndRegion(ras, m_shrink, m_region, savebox);
    if (savebox == TRect()) {
      if (m_region != TRect()) {
        ras = ras->create(m_region.getLx(), m_region.getLy());
        ras->clear();
        savebox = m_region;
      } else {
        // if both the savebox and the region are empty, do nothing
      }
    }
    ti->setCMapped(ras);
    ti->setSavebox(savebox);
    return ti;
  }
  return image;
}
//-------------------------------------------------------------------

const TImageInfo *TImageReaderTzl::getImageInfo() const {
  FILE *chan = m_lrp->m_chan;
  if (!chan) return 0;
  TzlOffsetMap::iterator it = m_lrp->m_frameOffsTable.find(m_fid);
  if (it == m_lrp->m_frameOffsTable.end()) return 0;
  FrameFields f;
  if (!readFrameFields(chan, it->second, m_lrp->m_res, f)) return 0;

  static TImageInfo info;
  info.m_x0   = f.sbx0;
  info.m_y0   = f.sby0;
  info.m_x1   = f.sbx0 + f.sblx - 1;
  info.m_y1   = f.sby0 + f.sbly - 1;
  info.m_lx   = m_lx;
  info.m_ly   = m_ly;
  info.m_dpix = f.xdpi;
  info.m_dpiy = f.ydpi;
  return &info;
}

//-------------------------------------------------------------------

TDimension TImageReaderTzl::getSize() const { return TDimension(m_lx, m_ly); }

//-------------------------------------------------------------------

TRect TImageReaderTzl::getBBox() const { return TRect(getSize()); }
