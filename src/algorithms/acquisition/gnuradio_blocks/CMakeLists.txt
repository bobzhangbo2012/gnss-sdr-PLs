# Copyright (C) 2012-2020  (see AUTHORS file for a list of contributors)
#
# GNSS-SDR is a software-defined Global Navigation Satellite Systems receiver
#
# This file is part of GNSS-SDR.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#


set(ACQ_GR_BLOCKS_SOURCES
    pcps_acquisition.cc
    pcps_assisted_acquisition_cc.cc
    pcps_acquisition_fine_doppler_cc.cc
    pcps_tong_acquisition_cc.cc
    pcps_cccwsr_acquisition_cc.cc
    pcps_quicksync_acquisition_cc.cc
    galileo_pcps_8ms_acquisition_cc.cc
    galileo_e5a_noncoherent_iq_acquisition_caf_cc.cc
)

set(ACQ_GR_BLOCKS_HEADERS
    pcps_acquisition.h
    pcps_assisted_acquisition_cc.h
    pcps_acquisition_fine_doppler_cc.h
    pcps_tong_acquisition_cc.h
    pcps_cccwsr_acquisition_cc.h
    pcps_quicksync_acquisition_cc.h
    galileo_pcps_8ms_acquisition_cc.h
    galileo_e5a_noncoherent_iq_acquisition_caf_cc.h
)

if(ENABLE_FPGA)
    set(ACQ_GR_BLOCKS_SOURCES ${ACQ_GR_BLOCKS_SOURCES} pcps_acquisition_fpga.cc)
    set(ACQ_GR_BLOCKS_HEADERS ${ACQ_GR_BLOCKS_HEADERS} pcps_acquisition_fpga.h)
endif()

if(ENABLE_OPENCL)
    set(ACQ_GR_BLOCKS_SOURCES ${ACQ_GR_BLOCKS_SOURCES} pcps_opencl_acquisition_cc.cc)
    set(ACQ_GR_BLOCKS_HEADERS ${ACQ_GR_BLOCKS_HEADERS} pcps_opencl_acquisition_cc.h)
endif()

list(SORT ACQ_GR_BLOCKS_HEADERS)
list(SORT ACQ_GR_BLOCKS_SOURCES)

source_group(Headers FILES ${ACQ_GR_BLOCKS_HEADERS})

add_library(acquisition_gr_blocks ${ACQ_GR_BLOCKS_SOURCES} ${ACQ_GR_BLOCKS_HEADERS})

if(${FILESYSTEM_FOUND})
    target_compile_definitions(acquisition_gr_blocks PRIVATE -DHAS_STD_FILESYSTEM=1)
    if(${find_experimental})
        target_compile_definitions(acquisition_gr_blocks PRIVATE -DHAS_STD_FILESYSTEM_EXPERIMENTAL=1)
    endif()
    target_link_libraries(acquisition_gr_blocks PRIVATE std::filesystem)
else()
    target_link_libraries(acquisition_gr_blocks PRIVATE Boost::filesystem)
endif()

target_link_libraries(acquisition_gr_blocks
    PUBLIC
        algorithms_libs
        acquisition_libs
        channel_libs
        core_system_parameters
        Armadillo::armadillo
        Gnuradio::runtime
        Gnuradio::fft
        Volk::volk
        Volkgnsssdr::volkgnsssdr
    PRIVATE
        Gflags::gflags
        Glog::glog
        Matio::matio
)

target_include_directories(acquisition_gr_blocks
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/algorithms/libs
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/receiver
)

if(GNURADIO_USES_STD_POINTERS)
    target_compile_definitions(acquisition_gr_blocks
        PUBLIC -DGNURADIO_USES_STD_POINTERS=1
    )
endif()

if(ENABLE_ARMA_NO_DEBUG)
    target_compile_definitions(acquisition_gr_blocks
        PUBLIC -DARMA_NO_BOUND_CHECKING=1
    )
endif()

if(ENABLE_OPENCL)
    target_link_libraries(acquisition_gr_blocks PUBLIC OpenCL::OpenCL)
    target_include_directories(acquisition_gr_blocks
        PUBLIC
            ${CMAKE_SOURCE_DIR}/src/algorithms/libs/opencl
    )
endif()

if(ENABLE_CLANG_TIDY)
    if(CLANG_TIDY_EXE)
        set_target_properties(acquisition_gr_blocks
            PROPERTIES
                CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
        )
    endif()
endif()

set_property(TARGET acquisition_gr_blocks
    APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/algorithms/libs/opencl>
)
