#ifndef SKINDESIGNER_SERVICE_H
#define SKINDESIGNER_SERVICE_H

#include <string>

class cSkindesignerService {
public:
    static void RegisterPlugin();
    static bool IsRegistered();
    static void InitOsd();
    static void CloseOsd();
    static void ClearGrids();
    static void Flush();
    static void DisplayViewElements();
    static void SetGrid(int index, const std::string& thumbnail, const std::string& albumname, bool is_folder, bool current, double x, double y, double width, double height);
};

#endif // SKINDESIGNER_SERVICE_H