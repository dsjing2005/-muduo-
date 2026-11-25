# 定义一个STC_LIST变量，包含了该目录下所有的源文件
aux_source_directory(. SRC_LIST)

# 指定由哪些源文件生成的哪个可执行文件名称
add_executable(ChatClient ${SRC_LIST})

# 指定可执行问及那连接时需要依赖的库文件
target_link_libraries(ChatClient pthread)