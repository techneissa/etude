# Uses the libraries built into -DETUDE_DEPS_PREFIX=DIR (see Dependencies.cmake) in place of
# building them. Included by Dependencies.cmake after it recorded the pins; every failure here is
# fatal, so a wrong prefix never turns into a quiet build from source.
if(NOT IS_DIRECTORY "${ETUDE_DEPS_PREFIX}" OR NOT EXISTS "${ETUDE_PREFIX_STAMP}")
  message(FATAL_ERROR "ETUDE_DEPS_PREFIX=${ETUDE_DEPS_PREFIX} is not a prefix built with "
    "-DETUDE_DEPS_INSTALL=ON (no share/etude-deps/stamp.txt)")
endif()

# Every library this configure needs must be in the prefix at the pin it was built from, and the
# prefix's compiler and build type must be this build's.
file(READ "${ETUDE_PREFIX_STAMP}" etude_prefix_text)
etude_prefix_stamp(etude_stamp)
string(REPLACE "\n" ";" etude_prefix_lines "${etude_prefix_text}")
string(REPLACE "\n" ";" etude_stamp_lines "${etude_stamp}")
foreach(line IN LISTS etude_stamp_lines)
  if(line AND NOT line IN_LIST etude_prefix_lines)
    message(FATAL_ERROR "ETUDE_DEPS_PREFIX=${ETUDE_DEPS_PREFIX} was built for something else.\n"
      "This configure needs: ${line}\nThe prefix has:\n${etude_prefix_text}\n"
      "Rebuild it with -DETUDE_DEPS_INSTALL=ON, or leave ETUDE_DEPS_PREFIX unset.")
  endif()
endforeach()

# Only the prefix is searched: a library of the system never stands in for a missing one.
macro(etude_find_in_prefix package)
  find_package(${package} CONFIG REQUIRED GLOBAL NO_DEFAULT_PATH PATHS "${ETUDE_DEPS_PREFIX}")
endmacro()
if(ETUDE_BUILD_APP)
  etude_find_in_prefix(SDL3)
  etude_find_in_prefix(SDL3_ttf)
  etude_find_in_prefix(etude-deps) # Verovio, lunasvg, plutovg
  set(ETUDE_VEROVIO_DATA "${ETUDE_DEPS_PREFIX}/share/verovio/data")
endif()
if(ETUDE_BUILD_APP OR ETUDE_BUILD_TOOLS)
  etude_find_in_prefix(libremidi)
endif()
