/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 * (C) 2003 Kai Tobias Burwieck <kai@burwieck.net>
 * based on MP3/MPlayer plugin to VDR (C++)
 * (C) 2001,2002 Stefan Huelswitt <huels@iname.com>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#ifndef ___DATA_H
#define ___DATA_H

#include <vdr/tools.h>
#include <vdr/config.h>

extern const char *g_szMountScript;

// ----------------------------------------------------------------

class cFileSource;

extern char *AddPath(const char *dir, const char *filename);

// ----------------------------------------------------------------

class cScanDir {
  char *QuoteString(const char *str);
protected:
  virtual ~cScanDir() {};
  enum eScanType { stFile, stDir };
  virtual void DoItem(cFileSource *src, const char *subdir, const char *name)=0;
public:
  bool ScanDir(cFileSource *src, const char *subdir, eScanType type, const char *spec, const char *excl, bool recursiv);
  };

// ----------------------------------------------------------------

enum eItemType { itDir, itParent, itFile, itBase };

class cDirItem
: public cListObject {
public:
  cDirItem(cFileSource *src, const char *subdir, const char *name, const eItemType type);
  ~cDirItem();
  char *Path(void);
  //
  cFileSource *Source;
  char *Subdir;
  char *Name;
  char *DisplayName;
  eItemType Type;
  bool HasFolderJpg;
  };

// ----------------------------------------------------------------

class cDirList
: public cScanDir
, public cList<cDirItem> {
  eItemType itype;
protected:
  virtual void DoItem(cFileSource *src, const char *subdir, const char *name);
public:
  bool Load(cFileSource *src, const char *subdir);
  };

// ----------------------------------------------------------------

class cFileSource
: public cListObject {
  enum eAction { acMount, acUnmount, acEject, acStatus };
  char *basedir;
  char *description;
  char *include;
  bool needsmount;
  int useCount;
  // remember last browse position
  char *browsedir, *browseparent;
  //
  void Set(const char *Basedir, const char *Description, const bool NeedsMount, const char *Include);
  bool Action(eAction act);
  void ClearRemember(void);
public:
  cFileSource(void);
  cFileSource(const char *Basedir, const char *Description, const bool NeedsMount, const char *Include=0);
  ~cFileSource();
  bool Parse(char *s);
  bool Mount(void);
  bool Unmount(void);
  bool Eject(void);
  bool Status(void);
  void Block(void) { ++useCount; }
  void Unblock(void) { --useCount; }
  char *BuildName(const char *filename);
  void SetRemember(const char *dir, const char *parent);
  bool GetRemember(char * &dir, char * &parent);
  const char *BaseDir(void) const { return basedir; }
  const char *Description(void) const { return description; }
  const char *Include(void) const { return include; }
  bool NeedsMount(void) const { return needsmount; }
  };

// ----------------------------------------------------------------

class cFileSources
: public cConfig<cFileSource> {
  cFileSource *current;
public:
  virtual bool Load(const char *filename, bool dummy=false);
  void SetSource(cFileSource *source) { current=source; }
  cFileSource *GetSource(void) { return current; }
  cFileSource *FindSource(const char *filename);
  };

#ifdef HAVE_LIBEXIF
void StopExifExtractor();
void ClearExifExtractorTasks();
#endif

#endif //___DATA_H
