add_executable(check_rels check_rels.c)
target_link_libraries(check_rels utils_with_io)
install(TARGETS check_rels RUNTIME DESTINATION ${LIBSUFFIX}/misc)

if (HAVE_GMPECM)
add_executable(descent_init_Fp descent_init_Fp.c smooth_detect.c)
target_link_libraries(descent_init_Fp utils ecm)
endif()

add_executable(debug_renumber EXCLUDE_FROM_ALL debug_renumber.c)
target_link_libraries(debug_renumber utils)

add_executable(extra_renumber EXCLUDE_FROM_ALL extra_renumber.c)
target_link_libraries(extra_renumber utils_with_io)

add_executable(renumber_convert EXCLUDE_FROM_ALL renumber_convert.c)
target_link_libraries(renumber_convert utils_with_io)

include(CMakeLists-nodist.txt OPTIONAL)
