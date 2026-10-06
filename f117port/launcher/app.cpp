// app.cpp -- the F-117A Nighthawk launcher: the game folder, the options
// and the options, then f117.
#include <wx/app.h>

#include "launcher.h"

class LauncherApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("F-117A Nighthawk");
        SetVendorName("Krzysztof Kania");
        if (!wxApp::OnInit()) return false;
        auto* frame = new LauncherFrame;
        SetTopWindow(frame);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(LauncherApp);
