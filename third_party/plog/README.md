# plog

The repository uses the header-only [`plog`](https://github.com/SergiusTheBest/plog) logger. Populate this directory with the library sources, e.g.:

```powershell
git submodule add https://github.com/SergiusTheBest/plog.git third_party/plog
```

Only the headers under `include` are required for compilation. The wrapper project calls into the logger conditionally; configure logging paths in `projects/Wrapper/src` as needed.
