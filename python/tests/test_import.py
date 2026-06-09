def test_import_package():
    import nanoprc_py

    assert hasattr(nanoprc_py, "Context")
