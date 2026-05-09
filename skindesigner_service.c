#include "skindesigner_service.h"
#include <skindesignerapi.h>
#include <pluginstructure.h>
#include <tokencontainer.h>

using namespace skindesignerapi;

static int g_SkindesignerPlugId = -1;
static bool g_SkindesignerRegistered = false;
static ISkinDisplayPlugin *g_displayPlugin = NULL;

void cSkindesignerService::RegisterPlugin() {
    if (g_SkindesignerRegistered) return;
    
    if (SkindesignerAPI::ServiceAvailable()) {
        cPluginStructure *ps = new cPluginStructure();
        ps->name = "image_next";
        ps->libskindesignerAPIVersion = "1.0"; 
        ps->RegisterRootView("grid");
        
        cTokenContainer *tkBg = new cTokenContainer();
        tkBg->CreateContainers();
        ps->RegisterViewElement(0, 0, "background", tkBg);
        
        cTokenContainer *tkHeader = new cTokenContainer();
        tkHeader->CreateContainers();
        ps->RegisterViewElement(0, 1, "header", tkHeader);
        
        cTokenContainer *tkDef = new cTokenContainer();
        tkDef->DefineStringToken("thumbnail", 0);
        tkDef->DefineStringToken("albumname", 1);
        tkDef->DefineIntToken("is_folder", 0);
        tkDef->DefineIntToken("current", 1);
        tkDef->CreateContainers();
        ps->RegisterViewGrid(0, 0, "imagegrid", tkDef);
        
        SkindesignerAPI::RegisterPlugin(ps);
        g_SkindesignerPlugId = ps->id;
        g_SkindesignerRegistered = true;
    }
}

bool cSkindesignerService::IsRegistered() {
    return g_SkindesignerRegistered;
}

void cSkindesignerService::InitOsd() {
    if (!g_SkindesignerRegistered) return;
    g_displayPlugin = SkindesignerAPI::GetDisplayPlugin(g_SkindesignerPlugId);
    if (g_displayPlugin) {
        g_displayPlugin->InitOsd();
        g_displayPlugin->Activate(0);
    }
}

void cSkindesignerService::CloseOsd() {
    if (g_displayPlugin) {
        g_displayPlugin->Deactivate(0, true);
        g_displayPlugin->CloseOsd();
        g_displayPlugin = NULL;
    }
}

void cSkindesignerService::DisplayViewElements() {
    if (!g_displayPlugin) return;
    
    cTokenContainer *tkBg = new cTokenContainer();
    tkBg->CreateContainers();
    g_displayPlugin->SetViewElementTokens(0, 0, tkBg);
    
    cTokenContainer *tkHeader = new cTokenContainer();
    tkHeader->CreateContainers();
    g_displayPlugin->SetViewElementTokens(1, 0, tkHeader);

    g_displayPlugin->DisplayViewElement(0, 0);
    g_displayPlugin->DisplayViewElement(1, 0);
}

void cSkindesignerService::ClearGrids() {
    if (g_displayPlugin) {
        g_displayPlugin->ClearGrids(0, 0);
    }
}

void cSkindesignerService::Flush() {
    if (g_displayPlugin) {
        g_displayPlugin->DisplayGrids(0, 0);
        g_displayPlugin->Flush();
    }
}

void cSkindesignerService::SetGrid(int index, const std::string& thumbnail, const std::string& albumname, bool is_folder, bool current, double x, double y, double width, double height) {
    if (!g_displayPlugin) return;
    
    cTokenContainer *tk = new cTokenContainer();
    tk->DefineStringToken("thumbnail", 0);
    tk->DefineStringToken("albumname", 1);
    tk->DefineIntToken("is_folder", 0);
    tk->DefineIntToken("current", 1);
    tk->CreateContainers();
    
    tk->AddStringToken(0, thumbnail.c_str());
    tk->AddStringToken(1, albumname.c_str());
    tk->AddIntToken(0, is_folder ? 1 : 0);
    tk->AddIntToken(1, current ? 1 : 0);
    
    g_displayPlugin->SetGrid(index, 0, 0, x, y, width, height, tk);
    
    if (current) {
        g_displayPlugin->SetGridCurrent(index, 0, 0, true);
    }
}