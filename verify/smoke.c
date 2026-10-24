// smoke.c -- proves the base can build what a native Aeon game links: SDL2 (Wayland), Vulkan, libaeon.
// It only creates a Vulkan instance and queries SDL; no window, so it runs headless in CI.
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include "aeon.h"

int main(void) {
    SDL_version v; SDL_GetVersion(&v);
    printf("SDL %d.%d.%d, libaeon %s\n", v.major, v.minor, v.patch, aeon_sdk_version());
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "aeon-smoke", .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VkResult r = vkCreateInstance(&ci, NULL, &inst);
    printf("vkCreateInstance: %d (no GPU in CI is fine; the link is what we test)\n", (int)r);
    if (r == VK_SUCCESS) vkDestroyInstance(inst, NULL);
    return 0;
}
