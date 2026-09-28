#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "traits.h"

//////////////////////////////////////
// Display Initialization

static int initDisplay(SDL_Window** win, SDL_Renderer** ren) {
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
		return -1;
	*win = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
							screen_width, screen_height, SDL_WINDOW_SHOWN);
	if (!*win)
		return -1;
	*ren = SDL_CreateRenderer(*win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!*ren) {
		SDL_DestroyWindow(*win);
		*win = NULL;
		return -1;
	}
	return 0;
}

static void destroyDisplay(SDL_Window* win, SDL_Renderer* ren, SDL_Texture* tex) {
	if (tex)
		SDL_DestroyTexture(tex);
	if (ren)
		SDL_DestroyRenderer(ren);
	if (win)
		SDL_DestroyWindow(win);
	SDL_Quit();
}

//////////////////////////////////////
// Splash Presentation

static void renderSplash(SDL_Renderer* ren, SDL_Texture* tex, int w, int h) {
	SDL_Rect dst = {
		.x = (screen_width - w) / 2,
		.y = (screen_height - h) / 2,
		.w = w,
		.h = h,
	};
	SDL_RenderClear(ren);
	if (screen_rotation)
		SDL_RenderCopyEx(ren, tex, NULL, &dst, screen_rotation, NULL, SDL_FLIP_NONE);
	else
		SDL_RenderCopy(ren, tex, NULL, &dst);
	SDL_RenderPresent(ren);
}

//////////////////////////////////////
// Application Entry

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: minui-show <image.png> [seconds]\n");
		return 1;
	}
	if (access(argv[1], R_OK) != 0 || MINIME_traitsInit() != 0)
		return 1;

	SDL_Window* window = NULL;
	SDL_Renderer* renderer = NULL;
	if (initDisplay(&window, &renderer) != 0) {
		SDL_Quit();
		return 1;
	}

	SDL_Surface* image = IMG_Load(argv[1]);
	SDL_Texture* texture = image ? SDL_CreateTextureFromSurface(renderer, image) : NULL;
	if (!texture) {
		if (image)
			SDL_FreeSurface(image);
		destroyDisplay(window, renderer, NULL);
		return 1;
	}

	renderSplash(renderer, texture, image->w, image->h);
	SDL_FreeSurface(image);

	int delay = argc > 2 ? atoi(argv[2]) : 2;
	sleep(delay);

	destroyDisplay(window, renderer, texture);
	return 0;
}
