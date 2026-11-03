# Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
# SPDX-License-Identifier: MIT

# Downloads raw volume data sets from the Open SciVis Datasets collection by Pavol Klacansky
# (https://klacansky.com/open-scivis-datasets/) into data/volumes/, where sample-volume looks for its default volume.
# The volumes are not part of the repository (data/ is ignored by git).
#
#   cmake -P cmake/download_volumes.cmake                        # the default volume: bonsai
#   cmake -DVOLUMES="neghip;foot;carp" -P cmake/download_volumes.cmake
#   cmake -DVOLUMES=all -P cmake/download_volumes.cmake         # everything in the table (about 3.4 GB)
#   cmake -DVOLUMES=list -P cmake/download_volumes.cmake        # only list the names
#
# Files that already exist are skipped. Each download is verified with the SHA-512 hash published on the web page of
# the collection. Most of these volumes are free to use for research, but "please cite individual datasets": see the
# acknowledgement and BibTeX of each data set on that page, and the table in README.md. Use the file names as they
# are: loadVolume() reads the size and data type from them.

cmake_minimum_required(VERSION 3.24) # script mode: also sets the policies, e.g. for IN_LIST

# name|file name|SHA-512 (the file is at <base url>/<name>/<file name>)
set(VOLUME_TABLE
    "neghip|neghip_64x64x64_uint8.raw|d00f95d1f4cb1da2f3260a722059527ace73514cd57e1c31b6724d18468fd81002f5da704a0c3c7ccf06a329a975dede38cfacf13387dea9ffd8d7542cae62d0"
    "bonsai|bonsai_256x256x256_uint8.raw|b34156a0ffc80ffaf84d069f3d05a40fdd999a35f05492829a2b0c13403a3147e73712b1d10c2cc34da66a59540a1632dae6adc96f3ebf3efa5d4d6c10598997"
    "foot|foot_256x256x256_uint8.raw|56a73bd1f694a09809688f5b0bbf0dfff0ef3e853a34c60c4d51bef02dd96fbfe5191f47c7762bdf7caab6f9a22f1d0e830652779f87c70cc519f345221edd0e"
    "skull|skull_256x256x256_uint8.raw|6b31b8c3c056e4fca2c908c8feffe1ad4872e1201feafe136ed22178d2593f549c4a8328b2f6ec7d7855271f28f7a4981ffae8ced9ff21df11652abe7c0766bc"
    "aneurism|aneurism_256x256x256_uint8.raw|31f1232cbf75b0182f172375e46cc57fe013c0edaae292f2358e2477052ca974faae92efd803a2d229172b851b5ddaa461129872b89bd6ce45d43bfe595eed43"
    "mri_woman|mri_woman_256x256x109_uint16.raw|f14941ef8e9cabd751fe2c433b4cfaef7ad846c05eeea8026d541e3e32dcc5b04cb04ea4f605188059d371d0967218bef9529429be53d9e88ff72f964f73938f"
    "tacc_turbulence|tacc_turbulence_256x256x256_float32.raw|2206fc1368064e62a752cea90b3932289a26606a3b1552b945745f3c1c57ded867ec8b28daa1d05bfd20438c8c3fa95460a43c37f086aaffea1f5b5742b72580"
    "carp|carp_256x256x512_uint16.raw|0ec57ca05d700e25a87cc2ca976c066f52e254c0269aa5fb7decb08ba3607cbf996b5d72aac8671dfe6589a794935d3fd820aaa756e6f6d32b84def15455fea5"
    "bunny|bunny_512x512x361_uint16.raw|58c8de50e5e9638278a36c9ddb10d0948721b5821e62dff3dada7b783341b6e5d346014b8545f504eb33952ea5c91e3ca93d10a5ce6a78b32ecabd10bb9438f3"
    "prone|prone_512x512x463_uint16.raw|c3a44cb47390ece603f1e86088b132ad7da7effe44afcf50c5c1f3a8697de36d39ea1a74619187f540e6663701518b0ab39b8e040911a41c37226fb80ccd5117"
    "magnetic_reconnection|magnetic_reconnection_512x512x512_float32.raw|16c663b89799e58292eea0f83c9f07f340f52283af2cdb8c72aa28a9e6368876f60415dc6b2dfe11b53f2efb050b703131d2995e589f857b52c6a6807ea22a13"
    "chameleon|chameleon_1024x1024x1080_uint16.raw|61c0309e87416d7ae6adf06bc11af969c8093a10b0a112caba5c6a672b40eac6aa81a9af20f33e00ef19c9d04493bc2fe37233e48f820b56be211b6d99e49ec3"
    "duct|duct_193x194x1000_float32.raw|18bc15ddbd477643460b4ff3e9303a7cbd5a87789605305d29e14f76019a13619821d2d823b0f59321b5c36dbaef019cf4127182bfaba87e87e42b33939a677c"
    "csafe_heptane|csafe_heptane_302x302x302_uint8.raw|5a749e5e16d1f521b3e6f9513c8bae562a18dbc4ec5e843310a0d89502571701f7fa43d1477bf13089020a6f345ed21d1ea51623d6ecb132887901651084252b"
)
set(BASE_URL "http://klacansky.com/open-scivis-datasets")

get_filename_component(OUT_DIR "${CMAKE_CURRENT_LIST_DIR}/../data/volumes" ABSOLUTE)

if(NOT VOLUMES)
    set(VOLUMES bonsai)
endif()

set(NAMES "")
foreach(ENTRY IN LISTS VOLUME_TABLE)
    string(REPLACE "|" ";" FIELDS "${ENTRY}")
    list(GET FIELDS 0 NAME)
    list(APPEND NAMES ${NAME})
endforeach()

if(VOLUMES STREQUAL "list")
    message(STATUS "Available volumes: ${NAMES}")
    return()
endif()
if(VOLUMES STREQUAL "all")
    set(VOLUMES ${NAMES})
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")
foreach(WANTED IN LISTS VOLUMES)
    if(NOT WANTED IN_LIST NAMES)
        message(FATAL_ERROR "Unknown volume '${WANTED}'. Available: ${NAMES}")
    endif()
    foreach(ENTRY IN LISTS VOLUME_TABLE)
        string(REPLACE "|" ";" FIELDS "${ENTRY}")
        list(GET FIELDS 0 NAME)
        if(NOT NAME STREQUAL WANTED)
            continue()
        endif()
        list(GET FIELDS 1 FILE_NAME)
        list(GET FIELDS 2 HASH)
        if(EXISTS "${OUT_DIR}/${FILE_NAME}")
            message(STATUS "${FILE_NAME} already exists.")
            continue()
        endif()
        # download to a temporary name, so that an aborted download is never mistaken for a volume
        message(STATUS "Downloading ${FILE_NAME} ...")
        file(DOWNLOAD "${BASE_URL}/${NAME}/${FILE_NAME}" "${OUT_DIR}/${FILE_NAME}.part"
             SHOW_PROGRESS STATUS RESULT EXPECTED_HASH SHA512=${HASH})
        list(GET RESULT 0 CODE)
        if(NOT CODE EQUAL 0)
            file(REMOVE "${OUT_DIR}/${FILE_NAME}.part")
            message(FATAL_ERROR "Download of ${FILE_NAME} failed: ${RESULT}")
        endif()
        file(RENAME "${OUT_DIR}/${FILE_NAME}.part" "${OUT_DIR}/${FILE_NAME}")
    endforeach()
endforeach()
