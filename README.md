# Radiary

jwildfire inspired prototype in C++

## Packaging

Build the release binaries:

```powershell
cmake --preset vs-release
cmake --build --preset vs-release
```

Create a portable ZIP:

```powershell
cpack --preset vs-release-portable
```

Create a Windows installer:

```powershell
cpack --preset vs-release-installer
```

The installer preset uses the CPack `NSIS` generator, so `makensis` needs to be installed and available on `PATH`.
