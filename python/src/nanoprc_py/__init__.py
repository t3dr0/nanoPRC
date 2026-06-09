from pathlib import Path
import os

_DLL_DIRECTORY_HANDLES = []


def _add_windows_dll_search_paths() -> None:
    if os.name != "nt" or not hasattr(os, "add_dll_directory"):
        return

    pkg_dir = Path(__file__).resolve().parent
    repo_root = pkg_dir.parents[2]
    candidates = [
        pkg_dir,
        repo_root / "build" / "bin" / "Release",
        repo_root / "build" / "bin" / "Debug",
        repo_root / "build" / "Release",
        repo_root / "build" / "Debug",
    ]

    for path in candidates:
        if path.is_dir():
            handle = os.add_dll_directory(str(path))
            if handle is not None:
                _DLL_DIRECTORY_HANDLES.append(handle)


_add_windows_dll_search_paths()


from ._core import (
    Context,
    Document,
    ModelNode,
    View,
    PRC_API_ERROR_MEMORY,
    PRC_API_ERROR_PARAMETER,
    PRC_API_ERROR_PARSER,
    PRC_API_ERROR_UNSUPPORTED,
)

__all__ = [
    "Context",
    "Document",
    "ModelNode",
    "View",
    "PRC_API_ERROR_MEMORY",
    "PRC_API_ERROR_PARAMETER",
    "PRC_API_ERROR_PARSER",
    "PRC_API_ERROR_UNSUPPORTED",
]
