# git_version(<target>): generates git_version.hpp for <target> with GIT_COMMIT (the git commit of the source directory,
# with "-dirty" for uncommitted changes) and BUILD_TYPE (Debug, Release, ..), e.g. for the results CSV of benchmark
# runs. The header is checked on every build, but only rewritten (and its users recompiled) if its content changes.
function(git_version target)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/${target}_generated/git_version.hpp)
    add_custom_target(${target}-git-version
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBUILD_TYPE=$<CONFIG>
                -DOUTPUT=${output} -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/write_git_version.cmake
        BYPRODUCTS ${output}
    )
    add_dependencies(${target} ${target}-git-version)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${target}_generated)
endfunction()
