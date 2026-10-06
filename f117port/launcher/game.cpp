// game.cpp -- the files the game needs and starting it.
#include "game.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <string>
#include <vector>

namespace {

// A file of `dir` whose name matches `name` as written, in upper or in lower case
// (case matters outside Windows); empty if there is none.
wxString FindFile(const wxString& dir, const wxString& name) {
    for (const wxString& n : {name, name.Upper(), name.Lower()}) {
        const wxString path = wxFileName(dir, n).GetFullPath();
        if (wxFileName::FileExists(path)) return path;
    }
    return wxString();
}

}  // namespace

bool FilePresent(const wxString& dir, const wxString& name) { return !dir.empty() && !FindFile(dir, name).empty(); }

wxString MissingGameFile(const wxString& dir) {
    for (const char* name : {"F117.COM", "SETUP.EXE", "MPS_LOGO.EXE", "PLAYER.EXE", "START.EXE", "VGAME.EXE", "END.EXE",
                             "DSWAP.EXE", "MGRAPHIC.EXE", "MISC.EXE"})
        if (!FilePresent(dir, name)) return name;
    return wxString();
}

wxString LauncherDir() { return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(); }

wxString DefaultGameDir() {
    // "Game" beside the launcher; in a source tree (launcher in f117port/launcher/build) the repository's Game
    // folder a few levels up.
    wxFileName dir(LauncherDir(), wxEmptyString);
    const wxString beside = wxFileName(LauncherDir(), "Game").GetFullPath();
    for (int up = 0; up < 5; ++up) {
        wxFileName game(dir);
        game.AppendDir("Game");
        if (FilePresent(game.GetPath(), "F117.COM")) return game.GetPath();
        if (dir.GetDirCount() == 0) break;
        dir.RemoveLastDir();
    }
    return beside;
}

wxString DefaultProgram() {
    wxFileName name(LauncherDir(), "f117");
#ifdef __WXMSW__
    name.SetExt("exe");
#endif
    return name.GetFullPath();
}

bool LaunchGame(const GameOptions& o, wxString& error) {
    if (!wxFileName::FileExists(o.program)) {
        error = wxString::Format("The game's program isn't there:\n\n%s", o.program);
        return false;
    }
    std::vector<wxString> args{o.program,
                               "--game-dir", o.gameDir,
                               "--scale", wxString::Format("%d", o.scale),
                               "--aspect", o.aspect43 ? "4:3" : "1:1"};
    if (o.fullscreen) args.push_back("--fullscreen");

    std::vector<std::wstring> wide;
    for (const wxString& a : args) wide.push_back(a.ToStdWstring());
    std::vector<const wchar_t*> argv;
    for (const std::wstring& w : wide) argv.push_back(w.c_str());
    argv.push_back(nullptr);

    wxExecuteEnv env;  // an empty variable map: the game inherits the launcher's environment
    env.cwd = wxFileName(o.program).GetPath();
    if (!o.sound) {
        // Sound off: SDL opens its silent audio driver, the game runs as with a sound card.
        wxGetEnvMap(&env.env);
        env.env["SDL_AUDIO_DRIVER"] = "dummy";
    }
    long pid;
    {
        wxLogNull quiet;  // wxExecute would show its own error box
        pid = wxExecute(argv.data(), wxEXEC_ASYNC, nullptr, &env);
    }
    if (pid == 0) {
        error = wxString::Format("Couldn't start %s.\n\n%s", wxFileName(o.program).GetFullName(),
                                 wxSysErrorMsgStr(wxSysErrorCode()));
        return false;
    }
    return true;
}
