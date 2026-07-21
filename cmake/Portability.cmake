# cmake/Portability.cmake
#
# One place for every platform-divergent build knob, so the rest of the tree (and purrc,
# which shells out to the C++ compiler when it builds a .purr program) stays platform-
# clean. Nothing here changes cheatah's behavior or interface — it only adjusts the flags,
# the loadable-module file extension, and the libraries linked after compilation, so the
# same compile→link→dlopen pipeline works on Linux, macOS, and Windows.
#
# Exposes:
#   CHEATAH_ARCH_FLAG        "optimize for this machine" flag (-march=native / -mcpu=native / "")
#   CHEATAH_MODULE_EXT       loadable-module extension (.so / .dylib / .dll)
#   CHEATAH_VECLIB_FLAG      -fveclib=… for the ndarray SIMD ufunc kernels (or "" -> scalar)
#   CHEATAH_PURRC_CXXFLAGS   list of flags purrc passes to the compiler (compile+link)
#   CHEATAH_PURRC_MATHLINK   list of link args appended AFTER the module archives
include(CheckCXXCompilerFlag)

# --- "optimize for this machine" -------------------------------------------------------
# x86 clang/gcc take -march=native; Apple-silicon (arm64) clang wants -mcpu=native.
set(CHEATAH_ARCH_FLAG "")
check_cxx_compiler_flag("-march=native" CHEATAH_HAS_MARCH_NATIVE)
if(CHEATAH_HAS_MARCH_NATIVE)
    set(CHEATAH_ARCH_FLAG "-march=native")
else()
    check_cxx_compiler_flag("-mcpu=native" CHEATAH_HAS_MCPU_NATIVE)
    if(CHEATAH_HAS_MCPU_NATIVE)
        set(CHEATAH_ARCH_FLAG "-mcpu=native")
    endif()
endif()

# --- loadable-module file extension ----------------------------------------------------
if(WIN32)
    set(CHEATAH_MODULE_EXT ".dll")
elseif(APPLE)
    set(CHEATAH_MODULE_EXT ".dylib")
else()
    set(CHEATAH_MODULE_EXT ".so")
endif()

# --- SIMD acceleration of the transcendentals ------------------------------------------
# The arch flag above already auto-vectorizes the algebraic kernels (sqrt, the products,
# the factorizations) on EVERY platform. This step additionally routes the transcendentals
# (exp/log/sin/cos/tan) through the platform's VECTOR libm, where one ships by default:
#   Linux/glibc : libmvec     -> -fveclib=libmvec   (vector symbols come in via -lm)
#   macOS       : Accelerate  -> -fveclib=Accelerate (link -framework Accelerate)
#   Windows     : Intel SVML  -> -fveclib=SVML        (opt-in: -DCHEATAH_WIN_SVML=ON; needs SVML)
# Where none is selected the transcendentals stay scalar — still correct (and the rest of
# the SIMD work via the arch flag is unaffected), just not vector-dispatched.
set(CHEATAH_VECLIB_FLAG "")
set(_cheatah_math_link "")
if(APPLE)
    check_cxx_compiler_flag("-fveclib=Accelerate" CHEATAH_HAS_ACCELERATE)
    if(CHEATAH_HAS_ACCELERATE)
        set(CHEATAH_VECLIB_FLAG "-fveclib=Accelerate")
        set(_cheatah_math_link "-framework" "Accelerate")
    endif()
elseif(WIN32)
    option(CHEATAH_WIN_SVML "Vectorize transcendentals on Windows via Intel SVML (needs SVML)" OFF)
    if(CHEATAH_WIN_SVML)
        check_cxx_compiler_flag("-fveclib=SVML" CHEATAH_HAS_SVML)
        if(CHEATAH_HAS_SVML)
            set(CHEATAH_VECLIB_FLAG "-fveclib=SVML")
        endif()
    endif()
elseif(UNIX)
    check_cxx_compiler_flag("-fveclib=libmvec" CHEATAH_HAS_LIBMVEC)
    if(CHEATAH_HAS_LIBMVEC)
        set(CHEATAH_VECLIB_FLAG "-fveclib=libmvec")
        # libmvec's vector symbols live in libm's linker script; link it after the archives.
        set(_cheatah_math_link "-lm")
    endif()
endif()

# --- the flags purrc passes to the C++ compiler (one compile+link invocation) ----------
set(CHEATAH_PURRC_CXXFLAGS "-std=c++20" "-O3" "-DNDEBUG" "-fno-math-errno" "-w")
if(CHEATAH_ARCH_FLAG)
    list(APPEND CHEATAH_PURRC_CXXFLAGS "${CHEATAH_ARCH_FLAG}")
endif()
# ELF symbol-interposition control is meaningful only on ELF targets (Linux et al.).
check_cxx_compiler_flag("-fno-semantic-interposition" CHEATAH_HAS_NO_SEMINTERP)
if(CHEATAH_HAS_NO_SEMINTERP AND NOT APPLE AND NOT WIN32)
    list(APPEND CHEATAH_PURRC_CXXFLAGS "-fno-semantic-interposition")
endif()
# NO -flto here, and the reason is not "we forgot" — it is an ABI hazard, so leave it off.
#
# The win is real and measured (12th Gen i7-12700H, `purrc --cxxflag -flto`): a per-character string
# walk went 1.77 -> 1.02 ns/char (1.74x), for a roughly FIXED ~0.08s of extra compile time. Cheatah is
# happy to buy runtime with build time, so cost is NOT the objection.
#
# The objection is that -flto makes the compiler emit LLVM BITCODE objects instead of ELF. Those link
# only when -flto is on the LINK line too. purrc's program path is a single compile+link invocation and
# would be fine — but these same flags feed `--emit-library`, whose libcheatah_<m>.a archives are
# linked by OUTSIDE builds that know nothing about LTO (godspeed's scripts/build_editor.sh is exactly
# this). Bitcode in a published archive fails there with "file format not recognized" — the very error
# CMake's own check_cxx_compiler_flag("-flto") hits, because it compiles with the flag and links
# without it.
#
# To land LTO properly: apply it ONLY to the one-shot program path (never to --emit-library output), or
# ship fat objects (-ffat-lto-objects) so archives stay linkable by non-LTO consumers. Either needs the
# flag list split by output kind, which it is not today.
#
# The stdlib object libraries already get -funroll-loops (CHEATAH_MODULE_OPTFLAGS below); user programs
# did not, which is an inconsistency, not a decision. Unlike -flto this changes no object format.
check_cxx_compiler_flag("-funroll-loops" CHEATAH_HAS_UNROLL)
if(CHEATAH_HAS_UNROLL)
    list(APPEND CHEATAH_PURRC_CXXFLAGS "-funroll-loops")
endif()
if(WIN32)
    # clang on Windows produces a DLL with -shared; symbols are exported via the
    # __declspec(dllexport) the codegen emits, so no -fPIC/-pthread (implicit/ignored).
    list(APPEND CHEATAH_PURRC_CXXFLAGS "-shared")
else()
    list(APPEND CHEATAH_PURRC_CXXFLAGS "-fPIC" "-shared" "-pthread")
endif()
set(CHEATAH_PURRC_MATHLINK ${_cheatah_math_link})

# --- optimization flags for the stdlib module object libraries -------------------------
set(CHEATAH_MODULE_OPTFLAGS -O3 -funroll-loops)
if(CHEATAH_ARCH_FLAG)
    list(APPEND CHEATAH_MODULE_OPTFLAGS ${CHEATAH_ARCH_FLAG})
endif()
# Compile options for ufunc_simd.cpp (the vector-math kernel TU): always -fno-math-errno,
# plus -fveclib=… where the platform has a vector libm.
set(CHEATAH_UFUNC_SIMD_FLAGS -fno-math-errno)
if(CHEATAH_VECLIB_FLAG)
    list(APPEND CHEATAH_UFUNC_SIMD_FLAGS ${CHEATAH_VECLIB_FLAG})
endif()

message(STATUS "cheatah portability: ext=${CHEATAH_MODULE_EXT} arch='${CHEATAH_ARCH_FLAG}' "
               "veclib='${CHEATAH_VECLIB_FLAG}' mathlink='${_cheatah_math_link}'")
