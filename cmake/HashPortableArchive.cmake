if(NOT DEFINED ARCHIVE OR NOT EXISTS "${ARCHIVE}")
    message(FATAL_ERROR "ARCHIVE must name the generated portable ZIP")
endif()

file(SHA256 "${ARCHIVE}" digest)
get_filename_component(archive_name "${ARCHIVE}" NAME)
file(WRITE "${ARCHIVE}.sha256" "${digest}  ${archive_name}\n")
