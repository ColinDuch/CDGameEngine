#include <engine/core/Application.hpp>
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    try {
        Application app;
        if (argc > 1) app.setStartupProject(argv[1]);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Erreur : " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}