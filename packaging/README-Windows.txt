Marrow portable editor (Windows x64)
====================================

Run marrow_editor_shell.exe from this folder. The application requires a
Windows 10 22H2 or Windows 11 x64 machine with an OpenGL 4.1 Core capable GPU
and current vendor graphics driver.

This is a portable folder, not an installer. Settings remain per-user under
%APPDATA%\Marrow\editor-settings.json unless MARROW_CONFIG_HOME is set to an
absolute override directory. Project and runtime formats are unchanged.

The bundled assets/fixtures content is sample data. THIRD_PARTY.md and the
licenses directory record source-vendored dependency notices. MANIFEST.sha256
authenticates the canonical folder; the adjacent ZIP and its .sha256 file are
transport artifacts only.
