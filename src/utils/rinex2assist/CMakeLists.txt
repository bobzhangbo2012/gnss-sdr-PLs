# Copyright (C) 2012-2020  (see AUTHORS file for a list of contributors)
#
# GNSS-SDR is a software-defined Global Navigation Satellite Systems receiver
#
# This file is part of GNSS-SDR.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

find_package(GPSTK QUIET)
if(NOT GPSTK_FOUND OR ENABLE_OWN_GPSTK)
    include(GNUInstallDirs)
    set(GPSTK_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty/gpstk-${GNSSSDR_GPSTK_LOCAL_VERSION}/install/${CMAKE_INSTALL_LIBDIR}/${CMAKE_FIND_LIBRARY_PREFIXES}gpstk${CMAKE_SHARED_LIBRARY_SUFFIX})
    set(GPSTK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty/gpstk-${GNSSSDR_GPSTK_LOCAL_VERSION}/install/include)
endif()


find_package(Boost COMPONENTS iostreams serialization QUIET)
if(CMAKE_VERSION VERSION_LESS 3.5)
    if(NOT TARGET Boost::iostreams)
        add_library(Boost::iostreams IMPORTED SHARED)
        set_property(TARGET Boost::iostreams PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})
        set_property(TARGET Boost::iostreams PROPERTY
            INTERFACE_LINK_LIBRARIES ${Boost_IOSTREAMS_LIBRARIES})
        set_property(TARGET Boost::iostreams PROPERTY
            IMPORTED_LOCATION ${Boost_IOSTREAMS_LIBRARIES})
    endif()
    if(NOT TARGET Boost::serialization)
        add_library(Boost::serialization IMPORTED SHARED)
        set_property(TARGET Boost::serialization PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})
        set_property(TARGET Boost::serialization PROPERTY
            INTERFACE_LINK_LIBRARIES ${Boost_SERIALIZARION_LIBRARIES})
        set_property(TARGET Boost::serialization PROPERTY
            IMPORTED_LOCATION ${Boost_SERIALIZARION_LIBRARIES})
    endif()
endif()


find_program(UNCOMPRESS_EXECUTABLE uncompress
    PATHS /bin
    /usr/bin
    /usr/sbin
)

if(Boost_FOUND)
    message(STATUS "The rinex2assist utility tool will be built when doing '${CMAKE_MAKE_PROGRAM_PRETTY_NAME}'")
    add_executable(rinex2assist ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
    set_property(TARGET rinex2assist PROPERTY CXX_STANDARD 14)  # Required by GPSTk
    # Do not show warnings raised by GPSTk v3.0.0
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(rinex2assist
            PUBLIC
                -Wno-deprecated -Wno-unused-parameter -Wno-sign-compare -Wno-type-limits -Wno-unused-but-set-variable -Wno-deprecated-copy -Wno-extra
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(rinex2assist
            PUBLIC
                -Wno-deprecated -Wno-unused-parameter -Wno-sign-compare
        )
    endif()

    target_link_libraries(rinex2assist
        PUBLIC
            Boost::iostreams
            Boost::serialization
            ${GPSTK_LIBRARY}
            Gflags::gflags
            Threads::Threads
            core_system_parameters
    )

    target_include_directories(rinex2assist
        PUBLIC
            ${GPSTK_INCLUDE_DIR}/gpstk
            ${GPSTK_INCLUDE_DIR}
    )

    if(NOT UNCOMPRESS_EXECUTABLE-NOTFOUND)
        target_compile_definitions(rinex2assist PUBLIC -DUNCOMPRESS_EXECUTABLE="${UNCOMPRESS_EXECUTABLE}")
    else()
        target_compile_definitions(rinex2assist PUBLIC -DUNCOMPRESS_EXECUTABLE="")
    endif()

    if(NOT GPSTK_FOUND OR ENABLE_OWN_GPSTK)
        add_dependencies(rinex2assist gpstk-${GNSSSDR_GPSTK_LOCAL_VERSION})
    endif()

    add_custom_command(TARGET rinex2assist POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rinex2assist>
            ${CMAKE_SOURCE_DIR}/install/$<TARGET_FILE_NAME:rinex2assist>
    )

    install(TARGETS rinex2assist
        RUNTIME DESTINATION bin
        COMPONENT "rinex2assist"
    )
else()
    message(STATUS "Boost Iostreams library not found.")
    message(STATUS " The rinex2assist utility tool will not be built.")
    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux|kFreeBSD|GNU")
        message(STATUS " You can install the Boost Iostreams library by doing:")
        if(${LINUX_DISTRIBUTION} MATCHES "Fedora" OR ${LINUX_DISTRIBUTION} MATCHES "Red Hat")
            message(STATUS " sudo yum install boost-iostreams")
        elseif(${LINUX_DISTRIBUTION} MATCHES "openSUSE")
            message(STATUS " sudo zypper install libboost_iostreams-devel")
        else()
            message(STATUS " sudo apt-get install libboost-iostreams-dev")
        endif()
    endif()
endif()

set(Boost_FOUND TRUE)  # trick for summary report
