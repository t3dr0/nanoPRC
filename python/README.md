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

This is intentionally minimal as a safe starting point.

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

## Quick smoke test

```python
import nanoprc_py

ctx = nanoprc_py.Context()
doc = ctx.open("sample.prc")
print(doc.is_open)
```

## Examples

From repo root, after `python -m pip install -e python`:

```bash
python python/examples/01_import_and_context.py
python python/examples/02_open_file.py path/to/model.prc
NANOPRC_SAMPLE=path/to/model.prc python python/examples/03_pytest_style_smoke.py
```

PowerShell equivalent for the third example:

```powershell
$env:NANOPRC_SAMPLE = "path/to/model.prc"
python python/examples/03_pytest_style_smoke.py
```

## Next steps

1. Add model-tree accessors (`prc_api_prep_model_tree`, `prc_api_create_model_tree`).
2. Add tessellation extraction wrappers with Python-native containers.
3. Add NumPy zero/low-copy paths for vertex/index buffers.
4. Add pytest fixtures with golden PRC files.
