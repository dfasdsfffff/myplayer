cmake_minimum_required(VERSION 3.0)

file(GLOB_RECURSE H_FILES *.h)
file(GLOB_RECURSE CPP_FILES *.cpp)
file(GLOB_RECURSE UI_FILES *.ui)

set(TARGET_NAME player-demo)

# cmake时更新ts文件
execute_process(COMMAND lupdate-qt5 -no-obsolete ${H_FILES} ${CPP_FILES} -ts
                        ${TRANSLATION_FILES})

# make时生成qm文件
qt5_add_translation(QM_FILES ${TRANSLATION_FILES})

add_executable(${TARGET_NAME} ${H_FILES} ${CPP_FILES} ${QM_FILES} ${UI_FILES}
                              "${PROJECT_SOURCE_DIR}/src/mainwid.qrc")


target_include_directories(
  ${TARGET_NAME}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
          ${PROJECT_BINARY_DIR}
          ${PROJECT_SOURCE_DIR}
          ${AVCODEC_INCLUDE_DIRS}
          ${AVFORMAT_INCLUDE_DIRS}
          ${AVUTIL_INCLUDE_DIRS}
          ${SWRESAMPLE_INCLUDE_DIRS}
          ${SWSCALE_INCLUDE_DIRS}
          ${AVDEVICE_INCLUDE_DIRS}
          ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(
  ${TARGET_NAME} PRIVATE
  Qt5::Widgets
  ${AVCODEC_LIBRARIES}
  ${AVFORMAT_LIBRARIES}
  ${AVUTIL_LIBRARIES}
  ${SWRESAMPLE_LIBRARIES}
  ${SWSCALE_LIBRARIES}
  ${AVDEVICE_LIBRARIES}
  ${SDL2_LIBRARIES}
)