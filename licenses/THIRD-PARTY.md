# Third-party components in the release

`F-117A Nighthawk.exe` (the launcher) and `f117.exe` (the game) are MIT licensed, see `..\LICENSE`.

The DLLs below are unmodified builds from MSYS2 (https://packages.msys2.org, mingw64 repository). You may
replace any of them with your own build. The source of each is on the upstream project named here.

## Used by f117.exe (the game)

| Component | Version | Licence file | Upstream |
|---|---|---|---|
| Nuked-OPL3 (OPL3 emulator, statically linked into `f117.exe`) | commit as in `third_party/nuked-opl3` | `Nuked-OPL3-LICENSE.txt` (LGPL 2.1 or later) | https://github.com/nukeykt/Nuked-OPL3 |
| `SDL3.dll` | SDL 3.x (mingw-w64-x86_64-sdl3) | `SDL3-LICENSE.txt` (zlib) | https://libsdl.org |
| `libiconv-2.dll` (needed by SDL3.dll) | GNU libiconv 1.19 | `libiconv-COPYING.LIB.txt` (LGPL 2.1 or later) | https://www.gnu.org/software/libiconv/ |

## Used by F-117A Nighthawk.exe (the launcher)

| Component | Version | Licence file | Upstream |
|---|---|---|---|
| `wxbase32u_gcc_custom.dll`, `wxmsw32u_core_gcc_custom.dll` | wxWidgets 3.2 (mingw-w64-x86_64-wxwidgets3.2-msw) | `wxWidgets-licence.txt` (wxWindows Library Licence 3.1) | https://www.wxwidgets.org |
| `libstdc++-6.dll`, `libgcc_s_seh-1.dll` | GCC runtime (mingw-w64-x86_64-gcc-libs) | `gcc-libs-COPYING3.txt`, `gcc-libs-COPYING.RUNTIME.txt` (GPL 3 with the GCC Runtime Library Exception) | https://gcc.gnu.org |
| `libwinpthread-1.dll` | mingw-w64 winpthreads | `libwinpthread-COPYING.txt` | https://www.mingw-w64.org |

## Image libraries loaded by wxWidgets

These are loaded by the wxWidgets DLLs (libtiff by the core DLL, the rest by libtiff or wxWidgets). The launcher
shows no images from them.

| DLL | Licence file | Upstream |
|---|---|---|
| `libpng16-16.dll` | `libpng-LICENSE.txt` | http://www.libpng.org |
| `libtiff-6.dll` | `libtiff-LICENSE.md` | http://www.simplesystems.org/libtiff |
| `libjpeg-8.dll` | `libjpeg-turbo-LICENSE.md` | https://libjpeg-turbo.org |
| `libjbig-0.dll` | `jbigkit-COPYING.txt` | https://www.cl.cam.ac.uk/~mgk25/jbigkit/ |
| `libLerc.dll` | `lerc-LICENSE.txt` | https://github.com/Esri/lerc |
| `libdeflate.dll` | `libdeflate-COPYING.txt` | https://github.com/ebiggers/libdeflate |
| `libwebp-7.dll`, `libsharpyuv-0.dll` | `libwebp-COPYING.txt` | https://developers.google.com/speed/webp |
| `libzstd.dll` | `zstd-LICENSE.txt` | https://facebook.github.io/zstd/ |
| `liblzma-5.dll` | `xz-COPYING.txt`, `xz-COPYING.0BSD.txt` | https://tukaani.org/xz/ |
| `libpcre2-16-0.dll` | `pcre2-LICENCE.md` | https://www.pcre.org |
| `zlib1.dll` | `zlib-LICENSE.txt` (zlib) | https://zlib.net |

The zip's `Game` folder is empty: the original game files are not included (see README.md, How to play).
