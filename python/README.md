# nanoprc-py (scaffold)

This directory contains an initial Python binding scaffold for nanoPRC using
`pybind11` + `scikit-build-core`.

## Current binding surface

- `nanoprc_py.Context`
  - Owns a `prc_context*` via RAII.
  - `open(path)` returns a `nanoprc_py.Document`.
  - `print_error_stack()` forwards to `prc_api_print_error_stack`.
- `nanoprc_py.Document`
  - Owns a `prc_api_data` handle and releases it automatically.
  - `is_open` property reports whether handle is valid.
  - `number_of_views`, `get_view(view_index)` for camera/view metadata.
  - `prepare_model_tree()`, `create_model_tree()`, `tessellation_counts()` for model-tree and tessellation counts.
  - `number_of_faces()`, `face_vertex_count()`, `face_vertices()` for face-level vertex access.
- `nanoprc_py.ModelNode`
  - Exposes model tree nodes and children.
- `nanoprc_py.View`
  - Exposes view name, transform matrix, and camera distance.

This is intentionally minimal but now includes initial model-tree and tessellation inspection support.

## Build prerequisites

1. Build the main nanoPRC library first:

```bash
cmake -S . -B build
cmake --build build --config Release
```

2. Install Python build dependencies:

```bash
python -m pip install --upgrade pip
python -m pip install scikit-build-core pybind11
```

> Windows note: prefer `python -m pip ...` instead of `pip ...` to avoid PATH/script location warnings. If you want an isolated environment, create one with:
>
> ```powershell
> py -3.13 -m venv .venv
> .\.venv\Scripts\Activate.ps1
> python -m pip install --upgrade pip
> python -m pip install scikit-build-core pybind11
> ```

## Build/install the Python package

From repo root:

```bash
python -m pip install -e python
```

## Debug build

To debug native issues, build both the main nanoPRC library and the Python extension in `Debug` mode.

1. Build the main library in Debug.

```powershell
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --config Debug
```

2. Build the Python extension in Debug.

```powershell
cd python
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --config Debug
```

If the Python build cannot find the debug `nano_prc` library, pass it explicitly:

```powershell
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DNANOPRC_LIBRARY=..\build-debug\bin\Debug\nano_prc.dll
cmake --build build-debug --config Debug
```

3. Ensure the debug `nano_prc.dll` is on the DLL search path when importing `nanoprc_py`.

On Windows, `python/src/__init__.py` already adds `build/Debug` and `build/bin/Debug` to the DLL search path, so placing the Debug DLL there is usually sufficient.

4. Run Python with the same interpreter you used to build the extension.

```powershell
python python/examples/06_tessellation_summary.py examples/cylinder.pdf
```

5. Debug in Visual Studio by launching `python.exe` and using the example script as the command arguments.

This lets you set breakpoints in `python/src/bindings.cpp` and the native nanoPRC sources such as `src/prc_api.c`.

## Quick smoke test

```python
import nanoprc_py

ctx = nanoprc_py.Context()
doc = ctx.open("examples/triangle.pdf")
print(doc.is_open)
```

## Examples

From repo root, after `python -m pip install -e python`:

```bash
python python/examples/01_import_and_context.py
python python/examples/02_open_file.py path/to/model.prc
NANOPRC_SAMPLE=path/to/model.prc python python/examples/03_pytest_style_smoke.py
python python/examples/04_list_views.py path/to/model.prc
python python/examples/05_model_tree_summary.py path/to/model.prc
python python/examples/06_tessellation_summary.py path/to/model.prc
```

PowerShell equivalent for the third example:

```powershell
$env:NANOPRC_SAMPLE = "path/to/model.prc"
python python/examples/03_pytest_style_smoke.py
```

## Next steps

- NumPy zero/low-copy paths for vertex/index buffers are now available.
- Face, material, and text primitive accessors are now exposed for richer tessellation export.
- Add pytest fixtures with golden PRC files for regression coverage.
- Add CI validation for the Python package build and example smoke tests.
