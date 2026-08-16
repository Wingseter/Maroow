if(NOT DEFINED ROOT OR NOT IS_DIRECTORY "${ROOT}")
    message(FATAL_ERROR "ROOT must name the staged portable directory")
endif()

file(GLOB_RECURSE staged_files
    LIST_DIRECTORIES false
    RELATIVE "${ROOT}"
    "${ROOT}/*")
list(REMOVE_ITEM staged_files "MANIFEST.sha256")
list(SORT staged_files)

set(manifest "")
foreach(relative_path IN LISTS staged_files)
    file(SHA256 "${ROOT}/${relative_path}" digest)
    string(APPEND manifest "${digest}  ${relative_path}\n")
endforeach()
file(WRITE "${ROOT}/MANIFEST.sha256" "${manifest}")
