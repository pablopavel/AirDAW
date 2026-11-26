# The Airwindows library (AirwindowsFX.dll) that AirDAW can load instead of its built-in copy, plus the tool that
# writes its manifest. Used by the main build (tests) and by fxlib/CMakeLists.txt (the release workflow).

# Build id of an airwin2rack checkout: commit date + short hash, e.g. "2026.11.22-91b3343".
function(airdaw_airwin_build_id dir outvar)
  execute_process(COMMAND git -C "${dir}" log -1 "--format=%cd-%h" "--date=format:%Y.%m.%d"
                  OUTPUT_VARIABLE id OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT id)
    set(id "unknown")
  endif()
  set(${outvar} "${id}" PARENT_SCOPE)
endfunction()

# root: the AirDAW source tree. Needs the airwin-registry target (add_subdirectory of airwin2rack) first.
function(airdaw_add_fxlib root build_id)
  add_library(airwindows_fxlib SHARED ${root}/core/src/fxlib_api.cpp ${root}/fxlib/fxlib_export.cpp)
  target_include_directories(airwindows_fxlib PRIVATE ${root}/core/include)
  target_link_libraries(airwindows_fxlib PRIVATE airwin-registry)
  target_compile_definitions(airwindows_fxlib PRIVATE AIRDAW_FXLIB_BUILD="${build_id}")
  set_target_properties(airwindows_fxlib PROPERTIES OUTPUT_NAME AirwindowsFX)
  add_executable(airwindows_fxlib_manifest ${root}/fxlib/fxlib_manifest.cpp)
  target_include_directories(airwindows_fxlib_manifest PRIVATE ${root}/core/include)
  target_link_libraries(airwindows_fxlib_manifest PRIVATE airwindows_fxlib)
endfunction()
