# Google Test

The Google Test sources are expected to live in this directory. To populate it, either:

```powershell
git submodule add https://github.com/google/googletest.git third_party/googletest
```

or clone manually:

```powershell
git clone https://github.com/google/googletest.git third_party/googletest
```

The Visual Studio solution compiles the bundled `gtest-all.cc` and `gtest_main.cc` sources directly from this path, so make sure the clone preserves the canonical layout (`googletest/include`, `googletest/src`).
