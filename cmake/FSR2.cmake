# Compile FidelityFX FSR2 2.2.1 API + DX12 backend from third_party/fsr2.
# Shader permutations are generated with FidelityFX_SC.exe (fetched, not vendored).

set(FSR2_ROOT "${CMAKE_SOURCE_DIR}/third_party/fsr2")
set(FSR2_API "${FSR2_ROOT}/ffx-fsr2-api")
set(FSR2_SC "${FSR2_ROOT}/tools/sc/FidelityFX_SC.exe")
set(FSR2_SHADER_DIR "${CMAKE_BINARY_DIR}/fsr2_shaders")

if(NOT EXISTS "${FSR2_API}/ffx_fsr2.h")
  message(FATAL_ERROR
    "WINDOOM_FSR2=ON but third_party/fsr2 is missing. Run fetch-fsr2.cmd first.")
endif()
if(NOT EXISTS "${FSR2_SC}")
  message(FATAL_ERROR
    "WINDOOM_FSR2=ON but FidelityFX_SC.exe is missing. Run fetch-fsr2.cmd first.")
endif()

file(MAKE_DIRECTORY "${FSR2_SHADER_DIR}")

set(FFX_SC_BASE_ARGS
  -reflection -deps=gcc -DFFX_GPU=1
  -DFFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
  -DFFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
)
set(FFX_SC_PERMUTATION_ARGS
  -DFFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}
  -DFFX_FSR2_OPTION_HDR_COLOR_INPUT={0,1}
  -DFFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}
  -DFFX_FSR2_OPTION_JITTERED_MOTION_VECTORS={0,1}
  -DFFX_FSR2_OPTION_INVERTED_DEPTH={0,1}
  -DFFX_FSR2_OPTION_APPLY_SHARPENING={0,1}
)
set(FFX_SC_DX12_ARGS
  -E CS -Wno-for-redefinition -Wno-ambig-lit-shift
  -DFFX_HLSL=1 -DFFX_HLSL_6_2=1
)

set(FSR2_PASS_SHADERS
  ffx_fsr2_tcr_autogen_pass
  ffx_fsr2_autogen_reactive_pass
  ffx_fsr2_accumulate_pass
  ffx_fsr2_compute_luminance_pyramid_pass
  ffx_fsr2_depth_clip_pass
  ffx_fsr2_lock_pass
  ffx_fsr2_reconstruct_previous_depth_pass
  ffx_fsr2_rcas_pass
)

set(FSR2_PERMUTATION_HEADERS)
foreach(PASS ${FSR2_PASS_SHADERS})
  set(SRC "${FSR2_API}/shaders/${PASS}.hlsl")
  set(WAVE32 "${FSR2_SHADER_DIR}/${PASS}_permutations.h")
  set(WAVE64 "${FSR2_SHADER_DIR}/${PASS}_wave64_permutations.h")

  add_custom_command(
    OUTPUT "${WAVE32}"
    COMMAND "${FSR2_SC}" ${FFX_SC_BASE_ARGS} ${FFX_SC_DX12_ARGS} ${FFX_SC_PERMUTATION_ARGS}
      -name=${PASS} -DFFX_HALF=0 -T cs_6_2
      -I "${FSR2_API}/shaders" -output=${FSR2_SHADER_DIR} "${SRC}"
    DEPENDS "${SRC}" "${FSR2_SC}"
    COMMENT "FSR2 SC ${PASS} wave32"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${WAVE64}"
    COMMAND "${FSR2_SC}" ${FFX_SC_BASE_ARGS} ${FFX_SC_DX12_ARGS} ${FFX_SC_PERMUTATION_ARGS}
      -name=${PASS}_wave64 "-DFFX_FSR2_PREFER_WAVE64=[WaveSize(64)]" -DFFX_HALF=0 -T cs_6_6
      -I "${FSR2_API}/shaders" -output=${FSR2_SHADER_DIR} "${SRC}"
    DEPENDS "${SRC}" "${FSR2_SC}"
    COMMENT "FSR2 SC ${PASS} wave64"
    VERBATIM
  )
  list(APPEND FSR2_PERMUTATION_HEADERS "${WAVE32}" "${WAVE64}")

  if(NOT PASS STREQUAL "ffx_fsr2_compute_luminance_pyramid_pass")
    set(WAVE32_16 "${FSR2_SHADER_DIR}/${PASS}_16bit_permutations.h")
    set(WAVE64_16 "${FSR2_SHADER_DIR}/${PASS}_wave64_16bit_permutations.h")
    add_custom_command(
      OUTPUT "${WAVE32_16}"
      COMMAND "${FSR2_SC}" ${FFX_SC_BASE_ARGS} ${FFX_SC_DX12_ARGS} ${FFX_SC_PERMUTATION_ARGS}
        -name=${PASS}_16bit -DFFX_HALF=1 -enable-16bit-types -T cs_6_2
        -I "${FSR2_API}/shaders" -output=${FSR2_SHADER_DIR} "${SRC}"
      DEPENDS "${SRC}" "${FSR2_SC}"
      COMMENT "FSR2 SC ${PASS} wave32 16-bit"
      VERBATIM
    )
    add_custom_command(
      OUTPUT "${WAVE64_16}"
      COMMAND "${FSR2_SC}" ${FFX_SC_BASE_ARGS} ${FFX_SC_DX12_ARGS} ${FFX_SC_PERMUTATION_ARGS}
        -name=${PASS}_wave64_16bit "-DFFX_FSR2_PREFER_WAVE64=[WaveSize(64)]"
        -DFFX_HALF=1 -enable-16bit-types -T cs_6_6
        -I "${FSR2_API}/shaders" -output=${FSR2_SHADER_DIR} "${SRC}"
      DEPENDS "${SRC}" "${FSR2_SC}"
      COMMENT "FSR2 SC ${PASS} wave64 16-bit"
      VERBATIM
    )
    list(APPEND FSR2_PERMUTATION_HEADERS "${WAVE32_16}" "${WAVE64_16}")
  endif()
endforeach()

add_custom_target(ffx_fsr2_shaders DEPENDS ${FSR2_PERMUTATION_HEADERS})

add_library(ffx_fsr2_api STATIC
  "${FSR2_API}/ffx_assert.cpp"
  "${FSR2_API}/ffx_fsr2.cpp"
)
target_include_directories(ffx_fsr2_api PUBLIC "${FSR2_API}")
target_compile_definitions(ffx_fsr2_api PUBLIC FFX_GCC)
target_compile_definitions(ffx_fsr2_api PRIVATE _UNICODE UNICODE)
set_target_properties(ffx_fsr2_api PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

add_library(ffx_fsr2_dx12 STATIC
  "${FSR2_API}/dx12/ffx_fsr2_dx12.cpp"
  "${FSR2_API}/dx12/shaders/ffx_fsr2_shaders_dx12.cpp"
)
target_include_directories(ffx_fsr2_dx12 PUBLIC
  "${FSR2_API}"
  "${FSR2_SHADER_DIR}"
)
target_compile_definitions(ffx_fsr2_dx12 PUBLIC FFX_GCC)
target_compile_definitions(ffx_fsr2_dx12 PRIVATE _UNICODE UNICODE)
target_link_libraries(ffx_fsr2_dx12 PUBLIC d3d12)
add_dependencies(ffx_fsr2_dx12 ffx_fsr2_shaders)
set_target_properties(ffx_fsr2_dx12 PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

if(MSVC)
  target_compile_options(ffx_fsr2_api PRIVATE /W3 /wd4244 /wd4267 /wd4996 /EHsc)
  target_compile_options(ffx_fsr2_dx12 PRIVATE /W3 /wd4244 /wd4267 /wd4996 /EHsc)
endif()
