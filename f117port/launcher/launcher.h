// launcher.h -- the launcher window: choose the game folder and the display and sound options, then
// Play (the port) or Play original (the 1991 game in DOSBox).
#pragma once

#include <wx/frame.h>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

extern const char* const APP_TITLE;

class LauncherFrame : public wxFrame {
public:
    LauncherFrame();

private:
    wxWindow* CreateMainPage(wxWindow* parent);
    void CreateMenus();
#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    // Checks the folder, the program and DOSBox and enables the Play buttons to match.
    void UpdateState();
    // Relabels the window sizes for the chosen aspect.
    void FillScales();
    void BrowseFolder();
    void BrowseProgram();
    void BrowseDosbox();
    void Play();
    void PlayOriginal();
    void About();
    void Preferences();
    void SetTopmost(bool on);
    void Save();

    wxTextCtrl* folder_ = nullptr;
    wxTextCtrl* program_ = nullptr;
    wxTextCtrl* dosbox_ = nullptr;
    wxStaticBitmap* statusIcon_ = nullptr;
    wxStaticText* statusNote_ = nullptr;
    wxChoice* aspect_ = nullptr;
    wxChoice* scale_ = nullptr;
    wxCheckBox* fullscreen_ = nullptr;
    wxCheckBox* sound_ = nullptr;
    wxButton* play_ = nullptr;
    wxButton* original_ = nullptr;
    bool topmost_ = false;
    bool loading_ = true;  // no edits are recorded while the window is built
};
