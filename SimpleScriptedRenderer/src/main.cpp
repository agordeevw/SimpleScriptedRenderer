#include <SDL.h>

#include "application.hpp"

extern "C" int main(int, char**)
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        1280, 720, SDL_WINDOW_RESIZABLE);
  if (!window)
  {
    return 1;
  }

  {
    application app{ window };
    app.main_loop();
  }

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
