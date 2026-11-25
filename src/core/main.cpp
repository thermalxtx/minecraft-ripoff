// Entry point.
#include "platform.h"
#include "game.h"

int main(int argc, char** argv) {
    Game game;
    if (!game.init(argc, argv)) return 1;
    game.run();
    game.shutdown();
    return 0;
}
