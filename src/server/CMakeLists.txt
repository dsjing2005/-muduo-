
# 定义一个SRC_LIST变量，包含了该目录下的所有源文件
aux_source_directory(. SRC_LIST)
aux_source_directory(./db DB_LIST)
aux_source_directory(./model MODEL_LIST)
aux_source_directory(./redis REDIS_LIST)
# 指定生成的可执行文件
add_executable(ChatServer ${SRC_LIST} ${DB_LIST} ${MODEL_LIST} ${REDIS_LIST})

# 查找hiredis库，并让构建产物能够加载它
find_library(HIREDIS_LIBRARY NAMES hiredis)
if(NOT HIREDIS_LIBRARY)
    message(FATAL_ERROR "hiredis library not found")
endif()
get_filename_component(HIREDIS_LIBRARY_DIR "${HIREDIS_LIBRARY}" DIRECTORY)
set_target_properties(ChatServer PROPERTIES BUILD_RPATH "${HIREDIS_LIBRARY_DIR}")

# 指定可执行文件链接时需要的库文件
target_link_libraries(ChatServer muduo_net muduo_base mysqlclient pthread "${HIREDIS_LIBRARY}")
