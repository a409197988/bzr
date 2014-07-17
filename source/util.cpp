#include "util.h"
#include <SDL.h>
#include <string>

using namespace std;

void throwSDLError()
{
    throw runtime_error(string("SDL_Init failed: ") + SDL_GetError());
}
