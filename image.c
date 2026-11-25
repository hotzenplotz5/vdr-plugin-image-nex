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
#ifdef HAVE_SKINDESIGNER
#include <libskindesignerapi/skindesignerapi.h>
#include <libskindesignerapi/pluginstructure.h>
#include <libskindesignerapi/tokencontainer.h>
#endif

static const char *VERSION        = "0.6.3-dev";

class cPluginImage : public cPlugin {
    cDirItem*      m_pServiceDirItem;
    cFileSource*   m_pServiceFileSource;
#ifdef HAVE_SKINDESIGNER
    skindesignerapi::cPluginStructure *m_pluginStructure;
#endif

public:
  cPluginImage();
  virtual ~cPluginImage();
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return tr("A Image Viewer plugin"); }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Start(void);
  virtual const char *MainMenuEntry(void) { return (ImageSetup.m_bHideMenu ? NULL : tr("Images")); }
  virtual cOsdObject *MainMenuAction(void);
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual bool Service(const char *Id, void *Data);
private:
  void RemoveServiceSource();
#ifdef HAVE_SKINDESIGNER
  void RegisterSkinDesigner(void);
#endif
};

bool cPluginImage::SetupParse(const char *szName, const char *szValue)
{
	return ImageSetup.SetupParse(szName,szValue);
}

const char *g_szConfigDirectory = "image_next";

const char *cPluginImage::CommandLineHelp(void)
{
  static char *help_str=0;
  
  free(help_str);    //                                     for easier orientation, this is column 80|
  if (asprintf(&help_str,"  -m CMD,   --mount=CMD    use CMD to mount/unmount/eject image sources\n"
                         "                           (default: \"%s\")\n"
                         "  -c PATH,  --config=PATH  to specify directory of configuration data\n"
                         "                           relative to VDR Plugin configuration directory\n"
                         "                           (default: \"%s\")\n",
                        g_szMountScript,
                        g_szConfigDirectory?g_szConfigDirectory:""
                        ) < 0) {
      help_str = 0;
  }
  return help_str;
}

bool cPluginImage::ProcessArgs(int argc, char *argv[])
{
  static struct option long_options[] = {
      { "mount",    required_argument, NULL, 'm' },
      { "config",   required_argument, NULL, 'c' },
      { NULL }
    };
    
  int c, option_index = 0;
  while((c=getopt_long(argc,argv,"m:c:",long_options,&option_index))!=-1) {
    switch (c) {
      case 'm': g_szMountScript=optarg;break;
      case 'c': g_szConfigDirectory=optarg;break;
      default:  return false;
      }
    }
  return true;
}

#ifdef HAVE_SKINDESIGNER
void cPluginImage::RegisterSkinDesigner(void)
{
  // The Skindesigner API might only become available after our Start().
  if (m_pluginStructure || !skindesignerapi::SkindesignerAPI::ServiceAvailable())
    return;

  skindesignerapi::cPluginStructure *structure = new skindesignerapi::cPluginStructure();
  structure->name = "image_next";
  structure->libskindesignerAPIVersion = LIBSKINDESIGNERAPIVERSION;
  structure->RegisterRootView("grid");

  skindesignerapi::cTokenContainer *background = new skindesignerapi::cTokenContainer();
  structure->RegisterViewElement(0, 0, "background", background);

  skindesignerapi::cTokenContainer *header = new skindesignerapi::cTokenContainer();
  header->DefineStringToken("{title}", 0);
  structure->RegisterViewElement(0, 1, "header", header);

  skindesignerapi::cTokenContainer *items = new skindesignerapi::cTokenContainer();
  items->DefineStringToken("{thumbnail}", 0);
  items->DefineStringToken("{albumname}", 1);
  items->DefineIntToken("{is_folder}", 0);
  items->DefineIntToken("{current}", 1);
  structure->RegisterViewGrid(0, 0, "imagegrid", items);

  if (!skindesignerapi::SkindesignerAPI::RegisterPlugin(structure)) {
    esyslog("imageplugin: Skindesigner registration failed; native grid is available");
    delete structure;
    return;
  }
  m_pluginStructure = structure;
  isyslog("imageplugin: registered Skindesigner view image_next/grid");
}
#endif

bool cPluginImage::Start(void)
{
  // JPEG/PNG browsing and direct true-color OSD output do not require an
  // MPEG-2 encoder. Leave the legacy path available when it is present.
  if (!cEncode::Register())
    esyslog("imageplugin: MPEG-2 encoder unavailable; direct OSD output remains available");
  
#ifdef HAVE_SKINDESIGNER
  RegisterSkinDesigner();
#endif

  cString szConfSource = AddDirectory(ConfigDirectory(g_szConfigDirectory),  "imagesources.conf");
  ImageSources.Load(szConfSource);

  // Fallback 1: Lade aus altem Ordner "image", falls der neue leer ist
  if(ImageSources.Count() < 1) {
    cString szOldConfSource = AddDirectory(ConfigDirectory("image"), "imagesources.conf");
    ImageSources.Load(szOldConfSource);
  }

  // Do not expose the complete filesystem when configuration is missing.
  if (ImageSources.Count() < 1)
    esyslog("imageplugin: no imagesources.conf entries; configure a photo source before browsing");
  
  return true;
}

cPluginImage::cPluginImage()
{
    m_pServiceDirItem = NULL;
    m_pServiceFileSource = NULL;
#ifdef HAVE_SKINDESIGNER
    m_pluginStructure = NULL;
#endif
}

cPluginImage::~cPluginImage()
{
  cEncode::UnRegister();
  StopThumbLoader();
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

cOsdObject *cPluginImage::MainMenuAction(void)
{
  if (!ImageSources.GetSource()) {
    cOsdMenu *menu = new cOsdMenu(tr("Images"));
    menu->Add(new cOsdItem(tr("No image sources configured")));
    return menu;
  }

  if (ImageSetup.m_nDisplayMode == 2) {
#ifdef HAVE_SKINDESIGNER
    RegisterSkinDesigner();
    if (m_pluginStructure &&
        skindesignerapi::SkindesignerAPI::GetDisplayPlugin(m_pluginStructure->id)) {
      return new cMenuImageSkinDesigner(ImageSources.GetSource(), m_pluginStructure);
    }
    isyslog("imageplugin: Skindesigner view not available in active skin; using native grid (see README)");
#else
    isyslog("imageplugin: built without Skindesigner API; using native grid");
#endif
    return new cMenuImageGrid(ImageSources.GetSource());
  }
  if (ImageSetup.m_nDisplayMode == 1)
    return new cMenuImageGrid(ImageSources.GetSource());
  return new cMenuImageBrowse();
}

cMenuSetupPage *cPluginImage::SetupMenu(void)
{
  return new cMenuSetupImage;
}

bool cPluginImage::Service(const char *Id, void *Data)
{
  if (Id && strcmp(Id, "Image-ShowImage-v1.0") == 0) {
    if (Data == NULL)
      return true;
    char* Basedir=strdup((char*)Data);
    char* Name=strrchr(Basedir, '/');
    if(Name==NULL) {
      free(Basedir);
      return false;
    }
    *Name++='\0';

    // Shut down any active image player first, so it doesn't hold references to the old service source!
#if APIVERSNUM < 20402
    if(cControl::Control())
        cControl::Shutdown();
#else
    bool bHasControl = false;
    {
        cMutexLock ControlMutexLock;
        if(cControl::Control(ControlMutexLock))
            bHasControl = true;
    }
    if(bHasControl)
        cControl::Shutdown();
#endif

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
