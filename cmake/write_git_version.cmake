# Writes OUTPUT with the git commit of SOURCE_DIR (+ "-dirty" for uncommitted changes) and BUILD_TYPE. Runs as a script
# (cmake -P) on every build, see git_version() in git_version.cmake. OUTPUT is only written if its content changes.
execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${SOURCE_DIR} OUTPUT_VARIABLE GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GIT_RESULT ERROR_QUIET)
if(NOT GIT_RESULT EQUAL 0)
    set(GIT_COMMIT "unknown")
else()
    execute_process(COMMAND git status --porcelain --untracked-files=no
        WORKING_DIRECTORY ${SOURCE_DIR} OUTPUT_VARIABLE GIT_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(GIT_STATUS)
        string(APPEND GIT_COMMIT "-dirty")
    endif()
endif()

set(CONTENT "#pragma once\n#define GIT_COMMIT \"${GIT_COMMIT}\"\n#define BUILD_TYPE \"${BUILD_TYPE}\"\n")
if(EXISTS ${OUTPUT})
    file(READ ${OUTPUT} OLD_CONTENT)
endif()
if(NOT CONTENT STREQUAL OLD_CONTENT)
    file(WRITE ${OUTPUT} "${CONTENT}")
endif()
