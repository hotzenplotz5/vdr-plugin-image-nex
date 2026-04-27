/*
 * Image plugin to VDR (C++)
 *
 * (C) 2003 Kai Tobias Burwieck <kai at burwieck.net>
 * (C) 2004     "Interpohl"  <interpohl at vdr-portal.de> 
 * (C) 2004     O. Kreuzinger <Onno at Kreuzinger.biz>
 * (C) 2004     A. Holzhammer for the massive script updates
 * (C) 2004-2011 Andreas Brachold <vdr07 at deltab.de>
 *  
 *  based on mp3/mplayer plugin by Stefan Hülswitt <huels at iname.com>
 *   
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <getopt.h>
#include <vdr/plugin.h>


#include "setup-image.h"
#include "data-image.h"
#include "menu-image.h"
#include "control-image.h"
#include <vdr/i18n.h>
#include "commands.h"
#include "liboutput/encode.h"

static const char *VERSION        = "0.5.0";

class cPluginImage : public cPlugin {
    cDirItem*      m_pServiceDirItem;
    cFileSource*   m_pServiceFileSource;

public:
  cPluginImage();
  virtual ~cPluginImage();
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return tr("A Image Viewer plugin"); }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Start(void);
  virtual const char *MainMenuEntry(void) { return (ImageSetup.m_bHideMenu ? NULL : tr("Images")); }
  virtual cOsdMenu *MainMenuAction(void);
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual bool Service(const char *Id, void *Data);
private:
  void RemoveServiceSource();
};

bool cPluginImage::SetupParse(const char *szName, const char *szValue)
{
	return ImageSetup.SetupParse(szName,szValue);
}

const char *g_szConfigDirectory = NULL;

const char *cPluginImage::CommandLineHelp(void)
{
  static char *help_str=0;
  
  free(help_str);    //                                     for easier orientation, this is column 80|
  asprintf(&help_str,"  -m CMD,   --mount=CMD    use CMD to mount/unmount/eject image sources\n"
                     "                           (default: \"%s\")\n"
                     "  -C CMD,   --convert=CMD  use CMD when converting images\n"
                     "                           (default: \"%s\")\n"
                     "  -c PATH,  --config=PATH  to specify directory of configuration data\n"
                     "                           relative to VDR Plugin configuration directory\n"
                     "                           (default: \"%s\")\n",
                    g_szMountScript,
                    g_szConvertScript,
                    g_szConfigDirectory?g_szConfigDirectory:""
                    );
  return help_str;
}

bool cPluginImage::ProcessArgs(int argc, char *argv[])
{
  static struct option long_options[] = {
      { "mount",    required_argument, NULL, 'm' },
      { "convert",  required_argument, NULL, 'C' },
      { "config",   required_argument, NULL, 'c' },
      { NULL }
    };
    
  int c, option_index = 0;
  while((c=getopt_long(argc,argv,"m:C:c:",long_options,&option_index))!=-1) {
    switch (c) {
      case 'm': g_szMountScript=optarg;break;
      case 'C': g_szConvertScript=optarg;break;
      case 'c': g_szConfigDirectory=optarg;break;
      default:  return false;
      }
    }
  return true;
}

bool cPluginImage::Start(void)
{
  if(!cEncode::Register()) {   
    return false;
  }
  
  cString szConfSource = AddDirectory(ConfigDirectory(g_szConfigDirectory),  "imagesources.conf");
  ImageSources.Load(szConfSource);
  if(ImageSources.Count()<1) {
    const char* sz = szConfSource;
    esyslog("imageplugin: you must have defined at least one source in %s",sz);
    return false;
    }
  
  return true;
}

cPluginImage::cPluginImage()
{
    m_pServiceDirItem = NULL;
    m_pServiceFileSource = NULL;
}

cPluginImage::~cPluginImage()
{
  cEncode::UnRegister();
  RemoveServiceSource();
}

void cPluginImage::RemoveServiceSource()
{
  if(m_pServiceDirItem) {
      delete m_pServiceDirItem;
      m_pServiceDirItem = NULL;
  }
  if(m_pServiceFileSource)
  {
      delete m_pServiceFileSource;
      m_pServiceFileSource = NULL;
  }
}

cOsdMenu *cPluginImage::MainMenuAction(void)
{
  if (ImageSetup.m_nDisplayMode == 1) {
      return new cMenuImageGrid(ImageSources.GetSource());
  }
  return new cMenuImageBrowse;
}

cMenuSetupPage *cPluginImage::SetupMenu(void)
{
  return new cMenuSetupImage;
}

bool cPluginImage::Service(const char *Id, void *Data = NULL)
{
  if (Id && strcmp(Id, "Image-ShowImage-v1.0") == 0) {
    if (Data == NULL)
      return true;
    char* Basedir=strdup((char*)Data);
    char* Name=strrchr(Basedir, '/');
    if(Name==NULL)
      return false;
    *Name++='\0';
    RemoveServiceSource();
    m_pServiceFileSource=new cFileSource(Basedir, "called via Service()", false);
    m_pServiceDirItem=new cDirItem(m_pServiceFileSource, NULL, Name, itFile);
    cSlideShow *newss = new cSlideShow(m_pServiceDirItem);
    free(Basedir);
    if(newss->Load() && newss->Count()) {
      cImageControl::SetSlideShow(newss);
      return true;
      }
    delete newss;
    RemoveServiceSource();
    }
  return false;
}


VDRPLUGINCREATOR(cPluginImage); // Don't touch this!
