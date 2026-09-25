add_executable(game src/main.cpp)

target_link_libraries(game PRIVATE engine)

target_link_libraries(engine PUBLIC assimp::assimp)