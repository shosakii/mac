# This directory is linked into scratch/self-organizing-mac-v7 on the VM.
set(self_organizing_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/self-organizing-64node-mac.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/model/control-window-scheduler.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/model/logical-clock.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/model/mac-airtime-monitor.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/model/data-trace-tag.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/model/time-sync-manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/model/topology-scenario.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/model/topology-control-header.cc
)

build_exec(
    EXECNAME self-organizing-64node-v7
    EXECNAME_PREFIX scratch_self-organizing-mac-v7_
    SOURCE_FILES "${self_organizing_sources}"
    LIBRARIES_TO_LINK ${libwifi}
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/self-organizing-mac-v7/
)
