if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
else()
    # Portable archives contain bin/ and share/ below one versioned directory.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
endif()
