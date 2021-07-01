cmake_minimum_required (VERSION 2.8)
project (swarmkv)
INCLUDE_DIRECTORIES(include)
AUX_SOURCE_DIRECTORY(src DIR_SRCS)     #把src目录下的所有源文件存到变量DIR_SRCS中

add_executable(swarmkv ${DIR_SRCS})   #生成可执行文件，swarmkv swarmkv.c

link_directories(/sur/local/lib) #添加动态连接库的路径
target_link_libraries(swarmkv -lcjson ) #添加libmxnet.so

#add_subdirectory (vendor)


