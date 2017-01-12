module.exports = {
  cmd: "cl",
  name: "x86",
  args: [
    "/D_UNICODE",
    "/DUNICODE",
    "/DWIN32",
    "/D_WINDOWS",
    "mousejump.cpp"
  ],
  sh: true,
  env: {
    INCLUDE: "/Program Files (x86)/Windows Kits/10/Include/10.0.14393.0/ucrt;/Program Files (x86)/Windows Kits/10/Include/10.0.14393.0/shared;/Program Files (x86)/Windows Kits/10/Include/10.0.14393.0/um;/Program Files (x86)/Windows Kits/10/Include/10.0.14393.0/winrt;/Program Files (x86)/Microsoft Visual Studio/2017/BuildTools/VC/Tools/MSVC/14.10.24728/include",
    LIB: "/Program Files (x86)/Windows Kits/10/Lib/10.0.14393.0/ucrt/x86;/Program Files (x86)/Windows Kits/10/Lib/10.0.14393.0/um/x86;/Program Files (x86)/Microsoft Visual Studio/2017/BuildTools/VC/Tools/MSVC/14.10.24728/lib/x86"
  },
  errorMatch: [
    "(?<file>.+)\\((?<line>\\d+)\\): (?<message>error.*)"
  ],
  warningMatch: [
    "^regexp1$"
  ],
  keymap: "f5",
  atomCommandName: "visualstudio:win32",
  postBuild: function (buildOutcome) {
    if (buildOutcome) {
      atom.commands.dispatch(atom.views.getView(atom.workspace), "script:run-with-profile");
    }
  }
};
